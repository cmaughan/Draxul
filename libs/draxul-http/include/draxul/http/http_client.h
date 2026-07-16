#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace draxul::http
{

// Platform clients must unblock a request within this budget after observing
// cancellation. Services cancel first and then join their worker threads.
inline constexpr auto kCancellationShutdownBudget = std::chrono::milliseconds(500);

class CancellationToken
{
public:
    CancellationToken() = default;

    [[nodiscard]] bool is_cancelled() const noexcept;

private:
    explicit CancellationToken(std::shared_ptr<std::atomic<bool>> state);

    std::shared_ptr<std::atomic<bool>> state_;
    friend class CancellationSource;
};

class CancellationSource
{
public:
    CancellationSource();

    [[nodiscard]] CancellationToken token() const;
    void cancel() const noexcept;

private:
    std::shared_ptr<std::atomic<bool>> state_;
};

struct Request
{
    std::string url;
    std::string user_agent = "Draxul/0.1";
    std::chrono::milliseconds connect_timeout = std::chrono::seconds(10);
    std::chrono::milliseconds overall_timeout = std::chrono::seconds(60);
    std::size_t max_response_bytes = 16 * 1024 * 1024;
};

struct Response
{
    int status_code = 0;
    std::string body;
    std::string error;
    bool cancelled = false;

    [[nodiscard]] bool ok() const noexcept
    {
        return !cancelled && error.empty() && status_code >= 200 && status_code < 300;
    }
};

class IHttpClient
{
public:
    virtual ~IHttpClient() = default;
    // Implementations must not invoke a command shell and must honor request
    // limits plus kCancellationShutdownBudget.
    [[nodiscard]] virtual Response get(const Request& request, CancellationToken cancellation) = 0;
};

[[nodiscard]] std::shared_ptr<IHttpClient> create_platform_http_client();

// Percent-encode one query value according to RFC 3986. UTF-8 bytes are
// encoded independently and spaces are %20 (never shell/form '+' quoting).
[[nodiscard]] std::string encode_query_component(std::string_view value);

} // namespace draxul::http
