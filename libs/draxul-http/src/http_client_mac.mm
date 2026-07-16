#include <draxul/http/http_client.h>

#import <Foundation/Foundation.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace draxul::http
{
namespace
{

struct AsyncState
{
    std::mutex mutex;
    std::condition_variable completed;
    bool done = false;
    __strong NSData* data = nil;
    __strong NSURLResponse* response = nil;
    __strong NSError* error = nil;
};

class FoundationHttpClient final : public IHttpClient
{
public:
    Response get(const Request& request, CancellationToken cancellation) override
    {
        Response response;
        if (request.url.empty() || request.max_response_bytes == 0)
        {
            response.error = "invalid HTTP request";
            return response;
        }
        if (cancellation.is_cancelled())
        {
            response.cancelled = true;
            response.error = "request cancelled";
            return response;
        }

        NSString* url_text = [[NSString alloc] initWithBytes:request.url.data()
            length:request.url.size() encoding:NSUTF8StringEncoding];
        NSURL* url = url_text ? [NSURL URLWithString:url_text] : nil;
        if (!url || (![[url scheme] isEqualToString:@"https"] && ![[url scheme] isEqualToString:@"http"]))
        {
            response.error = "invalid HTTP URL";
            return response;
        }

        NSURLSessionConfiguration* configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        configuration.timeoutIntervalForRequest = request.connect_timeout.count() / 1000.0;
        configuration.timeoutIntervalForResource = request.overall_timeout.count() / 1000.0;
        NSURLSession* session = [NSURLSession sessionWithConfiguration:configuration];
        NSMutableURLRequest* native_request = [NSMutableURLRequest requestWithURL:url];
        [native_request setHTTPMethod:@"GET"];
        NSString* user_agent = [[NSString alloc] initWithBytes:request.user_agent.data()
            length:request.user_agent.size() encoding:NSUTF8StringEncoding];
        if (user_agent)
            [native_request setValue:user_agent forHTTPHeaderField:@"User-Agent"];

        auto state = std::make_shared<AsyncState>();
        NSURLSessionDataTask* task = [session dataTaskWithRequest:native_request
            completionHandler:^(NSData* data, NSURLResponse* native_response, NSError* error) {
                std::lock_guard lock(state->mutex);
                state->data = data;
                state->response = native_response;
                state->error = error;
                state->done = true;
                state->completed.notify_one();
            }];
        [task resume];

        const auto deadline = std::chrono::steady_clock::now() + request.overall_timeout;
        bool did_complete = false;
        bool exceeded_byte_limit = false;
        {
            std::unique_lock lock(state->mutex);
            while (!state->done && !cancellation.is_cancelled()
                && std::chrono::steady_clock::now() < deadline)
            {
                state->completed.wait_for(lock, std::chrono::milliseconds(10));
                if ([task countOfBytesReceived] > static_cast<int64_t>(request.max_response_bytes))
                {
                    exceeded_byte_limit = true;
                    break;
                }
            }
            did_complete = state->done;
        }
        if (!did_complete || exceeded_byte_limit)
        {
            [task cancel];
            std::unique_lock lock(state->mutex);
            state->completed.wait_for(lock, std::chrono::milliseconds(250), [&] { return state->done; });
            did_complete = state->done;
        }
        [session invalidateAndCancel];

        if (cancellation.is_cancelled())
        {
            response.cancelled = true;
            response.error = "request cancelled";
            return response;
        }
        if (exceeded_byte_limit)
        {
            response.error = "HTTP response exceeded byte limit";
            return response;
        }
        if (!did_complete)
        {
            response.error = "HTTP request timed out";
            return response;
        }
        if (state->error)
        {
            response.error = [[state->error localizedDescription] UTF8String];
            return response;
        }
        if (![state->response isKindOfClass:[NSHTTPURLResponse class]])
        {
            response.error = "invalid HTTP response";
            return response;
        }
        response.status_code = static_cast<int>([(NSHTTPURLResponse*)state->response statusCode]);
        if ([state->data length] > request.max_response_bytes)
        {
            response.error = "HTTP response exceeded byte limit";
            return response;
        }
        response.body.assign(static_cast<const char*>([state->data bytes]), [state->data length]);
        if (response.status_code < 200 || response.status_code >= 300)
            response.error = "HTTP status " + std::to_string(response.status_code);
        return response;
    }
};

} // namespace

std::shared_ptr<IHttpClient> create_platform_http_client()
{
    return std::make_shared<FoundationHttpClient>();
}

} // namespace draxul::http
