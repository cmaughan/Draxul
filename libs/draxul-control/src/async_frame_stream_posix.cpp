#include <draxul/async_frame_stream.h>

#include "control_codec.h"

#include <draxul/control_plane.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace draxul
{
namespace
{

void set_error(AsyncFrameStreamError& error, std::string code,
    std::string message, int native_code = 0)
{
    error = { std::move(code), std::move(message),
        static_cast<uint32_t>(native_code) };
}

uint64_t fnv1a(std::string_view value)
{
    uint64_t result = 1469598103934665603ull;
    for (const unsigned char byte : value)
    {
        result ^= byte;
        result *= 1099511628211ull;
    }
    return result;
}

std::string stream_endpoint(std::string_view stream_id,
    const std::filesystem::path& runtime_directory)
{
    const std::string key = std::string(stream_id) + "\n"
        + runtime_directory.lexically_normal().generic_string();
    char suffix[32]{};
    std::snprintf(suffix, sizeof(suffix), "%016llx",
        static_cast<unsigned long long>(fnv1a(key)));
    // sockaddr_un::sun_path is only 104 bytes on macOS. Keep the socket name
    // compact so the normal per-user Application Support runtime directory
    // still fits, while the hash preserves stream identity.
    return (runtime_directory / (std::string(suffix) + ".sock")).string();
}

bool set_nonblocking(int descriptor)
{
    const int flags = ::fcntl(descriptor, F_GETFL, 0);
    return flags >= 0
        && ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
}

void configure_descriptor(int descriptor)
{
    const int flags = ::fcntl(descriptor, F_GETFD, 0);
    if (flags >= 0)
        ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    ::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE,
        &enabled, sizeof(enabled));
#endif
}

} // namespace

class AsyncFrameStreamConnection::Impl
{
public:
    explicit Impl(int value)
        : descriptor(value)
    {
    }

    ~Impl()
    {
        close();
        if (descriptor >= 0)
            ::close(descriptor);
    }

    void close()
    {
        if (!closed.exchange(true) && descriptor >= 0)
            ::shutdown(descriptor, SHUT_RDWR);
    }

    bool transfer(bool reading, void* data, size_t size,
        std::stop_token stop_token, AsyncFrameStreamError& error)
    {
        const auto write_deadline = std::chrono::steady_clock::now()
            + std::chrono::seconds(5);
        size_t offset = 0;
        while (offset < size)
        {
            if (closed || stop_token.stop_requested())
            {
                set_error(error, "cancelled", "Session stream I/O was cancelled.");
                return false;
            }
            if (!reading
                && std::chrono::steady_clock::now() >= write_deadline)
            {
                set_error(error, "deadline_exceeded",
                    "Session stream write exceeded its deadline.", ETIMEDOUT);
                return false;
            }
            pollfd ready{
                descriptor,
                static_cast<short>(reading ? POLLIN : POLLOUT),
                0,
            };
            const int polled = ::poll(&ready, 1, 50);
            if (polled < 0 && errno == EINTR)
                continue;
            if (polled == 0)
                continue;
            if (polled < 0)
            {
                set_error(error, "io_error",
                    "Session stream I/O wait failed.", errno);
                return false;
            }
            if ((ready.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0
                && (ready.revents & (POLLIN | POLLOUT)) == 0)
            {
                set_error(error, "closed",
                    "The Session stream peer closed the connection.");
                return false;
            }
            const ssize_t transferred = reading
                ? ::recv(descriptor, static_cast<char*>(data) + offset,
                      size - offset, 0)
                : ::send(descriptor, static_cast<const char*>(data) + offset,
                      size - offset,
#ifdef MSG_NOSIGNAL
                      MSG_NOSIGNAL
#else
                      0
#endif
                  );
            if (transferred < 0 && (errno == EINTR || errno == EAGAIN
                    || errno == EWOULDBLOCK))
                continue;
            if (transferred <= 0)
            {
                if (closed || stop_token.stop_requested())
                {
                    set_error(error, "cancelled",
                        "Session stream I/O was cancelled.", errno);
                }
                else if (transferred == 0 || errno == EPIPE
                    || errno == ECONNRESET)
                {
                    set_error(error, "closed",
                        "The Session stream peer closed the connection.", errno);
                }
                else
                {
                    set_error(error, "io_error",
                        "Session stream I/O failed.", errno);
                }
                return false;
            }
            offset += static_cast<size_t>(transferred);
        }
        error = {};
        return true;
    }

    int descriptor = -1;
    std::atomic<bool> closed = false;
};

AsyncFrameStreamConnection::AsyncFrameStreamConnection(
    std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
{
}

AsyncFrameStreamConnection::~AsyncFrameStreamConnection() = default;
AsyncFrameStreamConnection::AsyncFrameStreamConnection(
    AsyncFrameStreamConnection&&) noexcept = default;
AsyncFrameStreamConnection& AsyncFrameStreamConnection::operator=(
    AsyncFrameStreamConnection&&) noexcept = default;

bool AsyncFrameStreamConnection::read_frame(std::string& bytes,
    std::stop_token stop_token, AsyncFrameStreamError& error)
{
    std::array<uint8_t, 4> prefix{};
    if (!impl_ || !impl_->transfer(true, prefix.data(), prefix.size(),
                      stop_token, error))
    {
        if (impl_)
            impl_->close();
        return false;
    }
    const size_t size = control_detail::frame_size(prefix);
    if (size == 0 || size > kControlMaxMessageBytes)
    {
        set_error(error, "invalid_frame",
            "Session stream frame size is invalid.");
        impl_->close();
        return false;
    }
    bytes.resize(size);
    const bool ok = impl_->transfer(
        true, bytes.data(), bytes.size(), stop_token, error);
    if (!ok)
        impl_->close();
    return ok;
}

bool AsyncFrameStreamConnection::write_frame(std::string_view bytes,
    std::stop_token stop_token, AsyncFrameStreamError& error)
{
    if (!impl_ || bytes.empty() || bytes.size() > kControlMaxMessageBytes)
    {
        set_error(error, "invalid_frame",
            "Session stream frame size is invalid.");
        return false;
    }
    const auto prefix = control_detail::frame_prefix(bytes.size());
    if (!impl_->transfer(false, const_cast<uint8_t*>(prefix.data()),
            prefix.size(), stop_token, error))
    {
        impl_->close();
        return false;
    }
    const bool ok = impl_->transfer(false,
        const_cast<char*>(bytes.data()), bytes.size(), stop_token, error);
    if (!ok)
        impl_->close();
    return ok;
}

void AsyncFrameStreamConnection::close()
{
    if (impl_)
        impl_->close();
}

bool AsyncFrameStreamConnection::connected() const
{
    return impl_ && !impl_->closed;
}

std::unique_ptr<AsyncFrameStreamConnection> AsyncFrameStreamClient::connect(
    std::string_view endpoint, std::chrono::milliseconds timeout,
    AsyncFrameStreamError& error)
{
    if (endpoint.size() >= sizeof(sockaddr_un::sun_path))
    {
        set_error(error, "io_error", "Session stream endpoint is too long.",
            ENAMETOOLONG);
        return nullptr;
    }
    const int descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0 || !set_nonblocking(descriptor))
    {
        const int code = errno;
        if (descriptor >= 0)
            ::close(descriptor);
        set_error(error, "io_error",
            "Unable to create the Session stream socket.", code);
        return nullptr;
    }
    configure_descriptor(descriptor);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, endpoint.data(), endpoint.size());
    address.sun_path[endpoint.size()] = '\0';
    if (::connect(descriptor, reinterpret_cast<sockaddr*>(&address),
            sizeof(address))
        != 0)
    {
        if (errno != EINPROGRESS)
        {
            const int code = errno;
            ::close(descriptor);
            set_error(error, "io_error",
                "Unable to connect to the Session stream.", code);
            return nullptr;
        }
        const auto deadline = std::chrono::steady_clock::now()
            + std::max(timeout, std::chrono::milliseconds(1));
        bool connected = false;
        bool timed_out = true;
        int connect_error = 0;
        while (std::chrono::steady_clock::now() < deadline)
        {
            pollfd ready{ descriptor, POLLOUT, 0 };
            const int wait = ::poll(&ready, 1, 10);
            if (wait < 0 && errno == EINTR)
                continue;
            if (wait > 0)
            {
                int socket_error = 0;
                socklen_t size = sizeof(socket_error);
                if (::getsockopt(descriptor, SOL_SOCKET, SO_ERROR,
                        &socket_error, &size)
                        == 0
                    && socket_error == 0)
                {
                    connected = true;
                    timed_out = false;
                    break;
                }
                connect_error = socket_error;
                timed_out = false;
                break;
            }
        }
        if (!connected)
        {
            const int code = connect_error != 0 ? connect_error : errno;
            ::close(descriptor);
            set_error(error, timed_out ? "deadline_exceeded" : "io_error",
                timed_out
                    ? "Timed out connecting to the Session stream."
                    : "Unable to connect to the Session stream.",
                code);
            return nullptr;
        }
    }
    error = {};
    return std::unique_ptr<AsyncFrameStreamConnection>(
        new AsyncFrameStreamConnection(
            std::make_unique<AsyncFrameStreamConnection::Impl>(descriptor)));
}

class AsyncFrameStreamListener::Impl
{
public:
    ~Impl()
    {
        signal_stop();
        close_descriptor();
    }

    void signal_stop()
    {
        active = false;
    }

    void close_descriptor()
    {
        if (descriptor >= 0)
        {
            ::close(descriptor);
            descriptor = -1;
        }
        if (!endpoint_value.empty())
            ::unlink(endpoint_value.c_str());
    }

    std::string endpoint_value;
    std::atomic<bool> active = false;
    int descriptor = -1;
};

AsyncFrameStreamListener::AsyncFrameStreamListener()
    : impl_(std::make_unique<Impl>())
{
}

AsyncFrameStreamListener::~AsyncFrameStreamListener() = default;

bool AsyncFrameStreamListener::start(std::string_view stream_id,
    const std::filesystem::path& runtime_directory,
    AsyncFrameStreamError& error)
{
    impl_->signal_stop();
    impl_->close_descriptor();
    impl_->endpoint_value = stream_endpoint(stream_id, runtime_directory);
    if (impl_->endpoint_value.size() >= sizeof(sockaddr_un::sun_path))
    {
        set_error(error, "io_error", "Session stream endpoint is too long.",
            ENAMETOOLONG);
        return false;
    }
    const int descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0 || !set_nonblocking(descriptor))
    {
        const int code = errno;
        if (descriptor >= 0)
            ::close(descriptor);
        set_error(error, "io_error",
            "Unable to create the Session stream listener.", code);
        return false;
    }
    configure_descriptor(descriptor);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, impl_->endpoint_value.c_str(),
        impl_->endpoint_value.size() + 1);
    ::unlink(impl_->endpoint_value.c_str());
    if (::bind(descriptor, reinterpret_cast<sockaddr*>(&address),
            sizeof(address))
            != 0
        || ::chmod(impl_->endpoint_value.c_str(), 0600) != 0
        || ::listen(descriptor, 16) != 0)
    {
        const int code = errno;
        ::close(descriptor);
        ::unlink(impl_->endpoint_value.c_str());
        set_error(error, "io_error",
            "Unable to bind the Session stream listener.", code);
        return false;
    }
    impl_->descriptor = descriptor;
    impl_->active = true;
    error = {};
    return true;
}

std::unique_ptr<AsyncFrameStreamConnection> AsyncFrameStreamListener::accept(
    std::stop_token stop_token, AsyncFrameStreamError& error)
{
    while (impl_->active && !stop_token.stop_requested())
    {
        pollfd ready{ impl_->descriptor, POLLIN, 0 };
        const int wait = ::poll(&ready, 1, 50);
        if (wait < 0 && errno == EINTR)
            continue;
        if (wait == 0)
            continue;
        if (wait < 0)
        {
            if (!impl_->active)
                break;
            set_error(error, "io_error",
                "Session stream accept failed.", errno);
            return nullptr;
        }
        const int client = ::accept(impl_->descriptor, nullptr, nullptr);
        if (client < 0 && (errno == EINTR || errno == EAGAIN
                || errno == EWOULDBLOCK))
            continue;
        if (client < 0)
        {
            set_error(error, "io_error",
                "Session stream accept failed.", errno);
            return nullptr;
        }
        if (!set_nonblocking(client))
        {
            const int code = errno;
            ::close(client);
            set_error(error, "io_error",
                "Unable to configure the Session stream connection.", code);
            return nullptr;
        }
        configure_descriptor(client);
        error = {};
        return std::unique_ptr<AsyncFrameStreamConnection>(
            new AsyncFrameStreamConnection(
                std::make_unique<AsyncFrameStreamConnection::Impl>(client)));
    }
    set_error(error, "cancelled", "Session stream accept was cancelled.");
    return nullptr;
}

const std::string& AsyncFrameStreamListener::endpoint() const
{
    return impl_->endpoint_value;
}

void AsyncFrameStreamListener::stop()
{
    if (impl_)
        impl_->signal_stop();
}

bool AsyncFrameStreamListener::running() const
{
    return impl_ && impl_->active;
}

} // namespace draxul
