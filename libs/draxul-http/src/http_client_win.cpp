#include <draxul/http/http_client.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace draxul::http
{
namespace
{

class InternetHandle
{
public:
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET value)
        : value_(value)
    {
    }
    ~InternetHandle()
    {
        if (value_)
            WinHttpCloseHandle(value_);
    }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    [[nodiscard]] HINTERNET get() const { return value_; }
    [[nodiscard]] explicit operator bool() const { return value_ != nullptr; }

private:
    HINTERNET value_ = nullptr;
};

std::wstring utf8_to_wide(std::string_view text)
{
    if (text.empty())
        return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0)
        return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            text.data(), static_cast<int>(text.size()), result.data(), count)
        != count)
    {
        return {};
    }
    return result;
}

std::string windows_error(std::string_view operation)
{
    return std::string(operation) + " failed (WinHTTP error " + std::to_string(GetLastError()) + ")";
}

class WinHttpClient final : public IHttpClient
{
public:
    Response get(const Request& request, CancellationToken cancellation) override
    {
        Response response;
        if (cancellation.is_cancelled())
        {
            response.cancelled = true;
            response.error = "request cancelled";
            return response;
        }
        if (request.url.empty() || request.max_response_bytes == 0)
        {
            response.error = "invalid HTTP request";
            return response;
        }

        const std::wstring url = utf8_to_wide(request.url);
        const std::wstring user_agent = utf8_to_wide(request.user_agent);
        if (url.empty())
        {
            response.error = "URL is not valid UTF-8";
            return response;
        }

        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwSchemeLength = static_cast<DWORD>(-1);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts)
            || (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS))
        {
            response.error = "invalid HTTP URL";
            return response;
        }

        InternetHandle session(WinHttpOpen(
            user_agent.empty() ? L"Draxul/0.1" : user_agent.c_str(),
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session)
        {
            response.error = windows_error("WinHttpOpen");
            return response;
        }

        const int connect_ms = static_cast<int>(std::clamp<std::int64_t>(
            request.connect_timeout.count(), 1, std::numeric_limits<int>::max()));
        const int overall_ms = static_cast<int>(std::clamp<std::int64_t>(
            request.overall_timeout.count(), 1, std::numeric_limits<int>::max()));
        WinHttpSetTimeouts(session.get(), connect_ms, connect_ms, overall_ms, overall_ms);

        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        InternetHandle connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0));
        if (!connection)
        {
            response.error = windows_error("WinHttpConnect");
            return response;
        }

        std::wstring target(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength > 0)
            target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        if (target.empty())
            target = L"/";
        const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        std::atomic<HINTERNET> request_handle(WinHttpOpenRequest(connection.get(), L"GET",
            target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
        if (!request_handle.load())
        {
            response.error = windows_error("WinHttpOpenRequest");
            return response;
        }

        const auto deadline = std::chrono::steady_clock::now() + request.overall_timeout;
        std::atomic<bool> deadline_expired{ false };
        std::jthread cancel_watcher([&request_handle, &deadline_expired, cancellation, deadline](std::stop_token stop) {
            while (!stop.stop_requested() && !cancellation.is_cancelled()
                && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            const bool timed_out = !stop.stop_requested() && !cancellation.is_cancelled()
                && std::chrono::steady_clock::now() >= deadline;
            if (timed_out)
                deadline_expired.store(true, std::memory_order_release);
            if (cancellation.is_cancelled() || timed_out)
            {
                if (HINTERNET handle = request_handle.exchange(nullptr))
                    WinHttpCloseHandle(handle);
            }
        });
        const auto close_request = [&] {
            cancel_watcher.request_stop();
            if (HINTERNET handle = request_handle.exchange(nullptr))
                WinHttpCloseHandle(handle);
        };
        const auto fail = [&](std::string message) {
            response.cancelled = cancellation.is_cancelled();
            response.error = response.cancelled ? "request cancelled"
                : deadline_expired.load(std::memory_order_acquire) ? "HTTP request timed out"
                                                                  : std::move(message);
            close_request();
            return response;
        };

        HINTERNET handle = request_handle.load();
        if (!WinHttpSendRequest(handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        {
            return fail(windows_error("WinHttpSendRequest"));
        }
        handle = request_handle.load();
        if (!handle || !WinHttpReceiveResponse(handle, nullptr))
            return fail(windows_error("WinHttpReceiveResponse"));

        DWORD status = 0;
        DWORD status_size = sizeof(status);
        handle = request_handle.load();
        if (!handle || !WinHttpQueryHeaders(handle,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX))
        {
            return fail(windows_error("WinHttpQueryHeaders"));
        }
        response.status_code = static_cast<int>(status);

        std::vector<char> buffer(64 * 1024);
        for (;;)
        {
            if (cancellation.is_cancelled())
                return fail("request cancelled");
            DWORD count = 0;
            handle = request_handle.load();
            if (!handle || !WinHttpReadData(handle, buffer.data(),
                    static_cast<DWORD>(buffer.size()), &count))
            {
                return fail(windows_error("WinHttpReadData"));
            }
            if (count == 0)
                break;
            if (count > request.max_response_bytes - std::min(request.max_response_bytes, response.body.size()))
                return fail("HTTP response exceeded byte limit");
            response.body.append(buffer.data(), count);
        }

        close_request();
        if (cancellation.is_cancelled())
        {
            response.cancelled = true;
            response.error = "request cancelled";
            return response;
        }
        if (deadline_expired.load(std::memory_order_acquire)
            || std::chrono::steady_clock::now() >= deadline)
        {
            response.error = "HTTP request timed out";
            return response;
        }
        if (response.status_code < 200 || response.status_code >= 300)
            response.error = "HTTP status " + std::to_string(response.status_code);
        return response;
    }
};

} // namespace

std::shared_ptr<IHttpClient> create_platform_http_client()
{
    return std::make_shared<WinHttpClient>();
}

} // namespace draxul::http
