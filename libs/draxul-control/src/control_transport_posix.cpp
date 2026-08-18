#include "control_transport.h"

#include "control_codec.h"
#include "control_exact_io.h"

#include <draxul/control_plane.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace draxul::control_detail
{
namespace
{

constexpr auto kIoTimeout = std::chrono::seconds(5);

TransportError posix_error(TransportStage stage, int code,
    FailureClass classification, std::string message)
{
    return {
        .stage = stage,
        .domain = NativeDomain::Posix,
        .native_code = static_cast<uint32_t>(code),
        .classification = classification,
        .message = std::move(message),
    };
}

void set_close_on_exec(int fd)
{
    const int flags = ::fcntl(fd, F_GETFD, 0);
    if (flags >= 0)
        ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

void suppress_sigpipe(int fd)
{
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#else
    (void)fd;
#endif
}

ssize_t socket_send(int fd, const void* data, size_t size)
{
#ifdef MSG_NOSIGNAL
    return ::send(fd, data, size, MSG_NOSIGNAL);
#else
    return ::send(fd, data, size, 0);
#endif
}

TransportStatus server_read_exact(
    int fd, void* data, size_t size, TransportStage stage)
{
    return read_exact(
        [fd](void* destination, size_t remaining,
            TransportStage current_stage) {
            const ssize_t read
                = ::recv(fd, destination, remaining, 0);
            if (read > 0)
                return IoAttemptResult::progress(
                    static_cast<size_t>(read));
            if (read < 0 && errno == EINTR)
                return IoAttemptResult::retry();
            const int code = read == 0 ? ECONNRESET : errno;
            auto error = posix_error(current_stage, code,
                FailureClass::IoError, "Control request failed.");
            return read == 0
                ? IoAttemptResult::end_of_stream(std::move(error))
                : IoAttemptResult::failure(std::move(error));
        },
        data, size, stage);
}

TransportStatus server_write_exact(
    int fd, const void* data, size_t size, TransportStage stage)
{
    return write_exact(
        [fd](const void* source, size_t remaining,
            TransportStage current_stage) {
            const ssize_t written = socket_send(fd, source, remaining);
            if (written > 0)
                return IoAttemptResult::progress(
                    static_cast<size_t>(written));
            if (written < 0 && errno == EINTR)
                return IoAttemptResult::retry();
            const int code = written == 0 ? EPIPE : errno;
            auto error = posix_error(current_stage, code,
                FailureClass::IoError, "Control request failed.");
            return written == 0
                ? IoAttemptResult::end_of_stream(std::move(error))
                : IoAttemptResult::failure(std::move(error));
        },
        data, size, stage);
}

TransportStatus wait_for_socket(
    int fd, short events, ControlDeadline deadline, TransportStage stage)
{
    while (true)
    {
        const auto remaining = remaining_time(deadline);
        if (remaining.count() == 0)
        {
            return TransportStatus::failure(posix_error(stage, ETIMEDOUT,
                FailureClass::DeadlineExceeded,
                "The Draxul control request exceeded its deadline."));
        }
        pollfd descriptor{ fd, events, 0 };
        const int ready = ::poll(
            &descriptor, 1, static_cast<int>(remaining.count()));
        if (ready > 0)
        {
            if ((descriptor.revents & (events | POLLERR | POLLHUP)) != 0)
                return TransportStatus::success();
            return TransportStatus::failure(posix_error(stage, EIO,
                FailureClass::IoError, "Control request failed."));
        }
        if (ready == 0)
        {
            return TransportStatus::failure(posix_error(stage, ETIMEDOUT,
                FailureClass::DeadlineExceeded,
                "The Draxul control request exceeded its deadline."));
        }
        if (errno != EINTR)
        {
            return TransportStatus::failure(posix_error(stage, errno,
                FailureClass::IoError, "Control request failed."));
        }
    }
}

TransportStatus client_read_exact(int fd, void* data, size_t size,
    ControlDeadline deadline, TransportStage stage)
{
    return read_exact(
        [fd, deadline](void* destination, size_t remaining,
            TransportStage current_stage) {
            auto wait = wait_for_socket(
                fd, POLLIN, deadline, current_stage);
            if (!wait.ok)
                return IoAttemptResult::failure(std::move(wait.error));
            const ssize_t read
                = ::recv(fd, destination, remaining, 0);
            if (read > 0)
                return IoAttemptResult::progress(
                    static_cast<size_t>(read));
            if (read < 0
                && (errno == EINTR || errno == EAGAIN
                    || errno == EWOULDBLOCK))
            {
                return IoAttemptResult::retry();
            }
            const int code = read == 0 ? ECONNRESET : errno;
            auto error = posix_error(current_stage, code,
                FailureClass::IoError, "Control request failed.");
            return read == 0
                ? IoAttemptResult::end_of_stream(std::move(error))
                : IoAttemptResult::failure(std::move(error));
        },
        data, size, stage);
}

TransportStatus client_write_exact(int fd, const void* data, size_t size,
    ControlDeadline deadline, TransportStage stage)
{
    return write_exact(
        [fd, deadline](const void* source, size_t remaining,
            TransportStage current_stage) {
            auto wait = wait_for_socket(
                fd, POLLOUT, deadline, current_stage);
            if (!wait.ok)
                return IoAttemptResult::failure(std::move(wait.error));
            const ssize_t written = socket_send(fd, source, remaining);
            if (written > 0)
                return IoAttemptResult::progress(
                    static_cast<size_t>(written));
            if (written < 0
                && (errno == EINTR || errno == EAGAIN
                    || errno == EWOULDBLOCK))
            {
                return IoAttemptResult::retry();
            }
            const int code = written == 0 ? EPIPE : errno;
            auto error = posix_error(current_stage, code,
                FailureClass::IoError, "Control request failed.");
            return written == 0
                ? IoAttemptResult::end_of_stream(std::move(error))
                : IoAttemptResult::failure(std::move(error));
        },
        data, size, stage);
}

class PosixServerTransport final : public ServerTransport
{
public:
    ~PosixServerTransport() override { cleanup(); }

    TransportStatus prepare(std::string_view session_id,
        const std::filesystem::path& runtime_directory) override
    {
        runtime_directory_ = runtime_directory;
        endpoint_ = (runtime_directory
            / (endpoint_key(session_id) + ".sock"))
                        .string();
        sockaddr_un address{};
        if (endpoint_.size() >= sizeof(address.sun_path))
        {
            return TransportStatus::failure(posix_error(
                TransportStage::EndpointPrepare, ENAMETOOLONG,
                FailureClass::EndpointUnavailable,
                "Control socket path is too long."));
        }

        endpoint_in_use_ = false;
        listener_error_ = 0;
        claimed_ = false;
        abandoned_ = false;
        endpoint_lock_path_ = runtime_directory
            / (session_key(session_id) + ".control.lock");
        int descriptor = -1;
        int open_flags = O_RDWR | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
        open_flags |= O_CLOEXEC;
#endif
        do
        {
            descriptor = ::open(
                endpoint_lock_path_.c_str(), open_flags, 0600);
        } while (descriptor < 0 && errno == EINTR);
        if (descriptor < 0 && errno == EEXIST)
        {
            int existing_flags = O_RDWR;
#ifdef O_CLOEXEC
            existing_flags |= O_CLOEXEC;
#endif
            do
            {
                descriptor = ::open(
                    endpoint_lock_path_.c_str(), existing_flags);
            } while (descriptor < 0 && errno == EINTR);
        }
        if (descriptor < 0)
        {
            return TransportStatus::failure(posix_error(
                TransportStage::EndpointPrepare, errno,
                FailureClass::EndpointUnavailable,
                "Unable to open the control endpoint lock."));
        }
        set_close_on_exec(descriptor);
        ::fchmod(descriptor, 0600);
        int lock_result = -1;
        do
        {
            lock_result = ::flock(descriptor, LOCK_EX | LOCK_NB);
        } while (lock_result != 0 && errno == EINTR);
        if (lock_result != 0)
        {
            const int code = errno;
            ::close(descriptor);
            const bool taken = code == EWOULDBLOCK || code == EAGAIN;
            endpoint_in_use_ = taken;
            return TransportStatus::failure(posix_error(
                TransportStage::EndpointClaim, code,
                FailureClass::EndpointUnavailable,
                taken
                    ? "Control endpoint is already in use by another Draxul instance."
                    : "Unable to lock the control endpoint."));
        }
        endpoint_lock_ = descriptor;
        return TransportStatus::success();
    }

    void run(std::stop_token stop_token,
        const ServerFrameHandler& handle_frame,
        const StartupReporter& report_startup) override
    {
        const int server = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (server < 0)
        {
            report_startup("Unable to create the control socket.");
            return;
        }
        set_close_on_exec(server);
        suppress_sigpipe(server);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, endpoint_.c_str(), endpoint_.size() + 1);

        if (::access(endpoint_.c_str(), F_OK) == 0)
        {
            bool in_use = false;
            const int probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (probe >= 0)
            {
                set_close_on_exec(probe);
                suppress_sigpipe(probe);
                in_use = ::connect(probe,
                             reinterpret_cast<sockaddr*>(&address),
                             sizeof(address))
                    == 0;
                ::close(probe);
            }
            if (in_use)
            {
                ::close(server);
                endpoint_in_use_ = true;
                report_startup(
                    "Control endpoint is already in use by another Draxul instance.");
                return;
            }
            ::unlink(endpoint_.c_str());
        }

        if (::bind(server, reinterpret_cast<sockaddr*>(&address),
                sizeof(address))
            != 0)
        {
            const int code = errno;
            ::close(server);
            const bool taken = code == EADDRINUSE;
            endpoint_in_use_ = taken;
            report_startup(taken
                    ? "Control endpoint is already in use by another Draxul instance."
                    : "Unable to bind the control socket.");
            return;
        }
        claimed_ = true;
        if (::chmod(endpoint_.c_str(), 0600) != 0
            || ::listen(server, 4) != 0)
        {
            ::close(server);
            ::unlink(endpoint_.c_str());
            claimed_ = false;
            report_startup("Unable to prepare the control socket.");
            return;
        }
        report_startup({});
        const int flags = ::fcntl(server, F_GETFL, 0);
        ::fcntl(server, F_SETFL, flags | O_NONBLOCK);

        auto serve_connections
            = [this, server, &handle_frame](std::stop_token shared_stop) {
                  while (!shared_stop.stop_requested())
                  {
                      pollfd descriptor{ server, POLLIN, 0 };
                      const int ready = ::poll(&descriptor, 1, 100);
                      if (ready <= 0)
                          continue;
                      const int client = ::accept(server, nullptr, nullptr);
                      if (client < 0)
                          continue;
                      set_close_on_exec(client);
                      suppress_sigpipe(client);
                      const int client_flags = ::fcntl(client, F_GETFL, 0);
                      if (client_flags >= 0
                          && (client_flags & O_NONBLOCK) != 0)
                      {
                          ::fcntl(client, F_SETFL,
                              client_flags & ~O_NONBLOCK);
                      }
                      timeval timeout{
                          static_cast<long>(kIoTimeout.count()), 0
                      };
                      ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                          &timeout, sizeof(timeout));
                      ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                          &timeout, sizeof(timeout));

                      std::string bytes;
                      const auto read_status = read_control_frame(
                          [client](void* data, size_t size,
                              TransportStage stage) {
                              return server_read_exact(
                                  client, data, size, stage);
                          },
                          bytes);
                      const std::optional<std::string> request
                          = read_status.ok
                          ? std::optional<std::string>(std::move(bytes))
                          : std::nullopt;
                      const std::string response = handle_frame(request);
                      write_control_frame(
                          [client](const void* data, size_t size,
                              TransportStage stage) {
                              return server_write_exact(
                                  client, data, size, stage);
                          },
                          response);
                      ::close(client);
                  }
              };

        std::vector<std::jthread> additional_listeners;
        additional_listeners.reserve(3);
        for (int i = 0; i < 3; ++i)
        {
            additional_listeners.emplace_back(
                [serve_connections, stop_token](std::stop_token) {
                    serve_connections(stop_token);
                });
        }
        serve_connections(stop_token);
        additional_listeners.clear();
        ::close(server);
    }

    const std::string& endpoint() const override { return endpoint_; }
    bool endpoint_in_use() const override { return endpoint_in_use_; }
    void abandon_endpoint() override { abandoned_ = true; }

    void cleanup() override
    {
        if (claimed_ && !abandoned_ && !endpoint_.empty())
            ::unlink(endpoint_.c_str());
        claimed_ = false;
        if (endpoint_lock_ >= 0)
        {
            ::flock(endpoint_lock_, LOCK_UN);
            ::close(endpoint_lock_);
            endpoint_lock_ = -1;
        }
    }

    uint32_t take_listener_error() override
    {
        return listener_error_.exchange(0);
    }

private:
    std::filesystem::path runtime_directory_;
    std::filesystem::path endpoint_lock_path_;
    std::string endpoint_;
    int endpoint_lock_ = -1;
    std::atomic<bool> endpoint_in_use_ = false;
    std::atomic<uint32_t> listener_error_ = 0;
    bool claimed_ = false;
    bool abandoned_ = false;
};

} // namespace

TransportStatus secure_runtime_directory(
    const std::filesystem::path& runtime_directory)
{
    // Preserve the existing POSIX behavior: chmod is best effort. Endpoint and
    // metadata creation still apply explicit 0600 modes below.
    ::chmod(runtime_directory.c_str(), 0700);
    return TransportStatus::success();
}

TransportStatus write_current_user_metadata(
    const std::filesystem::path& path, std::string_view contents)
{
    std::filesystem::path temporary = path;
    temporary += ".tmp-" + random_token();
    int flags = O_CREAT | O_EXCL | O_WRONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = ::open(temporary.c_str(), flags, 0600);
    if (fd < 0)
    {
        return TransportStatus::failure(posix_error(
            TransportStage::MetadataCreate, errno,
            FailureClass::EndpointUnavailable,
            "Unable to create temporary control metadata file."));
    }
    set_close_on_exec(fd);
    if (::fchmod(fd, 0600) != 0)
    {
        const int code = errno;
        ::close(fd);
        ::unlink(temporary.c_str());
        return TransportStatus::failure(posix_error(
            TransportStage::MetadataCreate, code,
            FailureClass::EndpointUnavailable,
            "Unable to secure temporary control metadata file."));
    }
    size_t offset = 0;
    while (offset < contents.size())
    {
        const ssize_t written = ::write(
            fd, contents.data() + offset, contents.size() - offset);
        if (written <= 0)
        {
            const int code = errno;
            ::close(fd);
            ::unlink(temporary.c_str());
            return TransportStatus::failure(posix_error(
                TransportStage::MetadataWrite, code,
                FailureClass::EndpointUnavailable,
                "Unable to write control metadata file."));
        }
        offset += static_cast<size_t>(written);
    }
    if (::fsync(fd) != 0)
    {
        const int code = errno;
        ::close(fd);
        ::unlink(temporary.c_str());
        return TransportStatus::failure(posix_error(
            TransportStage::MetadataFlush, code,
            FailureClass::EndpointUnavailable,
            "Unable to flush control metadata file."));
    }
    ::close(fd);
    if (::rename(temporary.c_str(), path.c_str()) != 0)
    {
        const int code = errno;
        ::unlink(temporary.c_str());
        return TransportStatus::failure(posix_error(
            TransportStage::MetadataReplace, code,
            FailureClass::EndpointUnavailable,
            "Unable to atomically replace control metadata file."));
    }
    const auto parent = path.parent_path().empty()
        ? std::filesystem::path(".")
        : path.parent_path();
    int directory_flags = O_RDONLY;
#ifdef O_DIRECTORY
    directory_flags |= O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
    directory_flags |= O_CLOEXEC;
#endif
    const int directory = ::open(parent.c_str(), directory_flags);
    if (directory < 0 || ::fsync(directory) != 0)
    {
        const int code = errno;
        if (directory >= 0)
            ::close(directory);
        return TransportStatus::failure(posix_error(
            TransportStage::MetadataDirectoryFlush, code,
            FailureClass::EndpointUnavailable,
            "Unable to flush the control metadata directory."));
    }
    ::close(directory);
    return TransportStatus::success();
}

ClientExchangeResult client_exchange(std::string_view endpoint,
    std::string_view request_bytes, ControlDeadline deadline)
{
    const int socket_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        return {
            .ok = false,
            .error = posix_error(TransportStage::EndpointPrepare, errno,
                FailureClass::EndpointUnavailable,
                "Unable to create control socket."),
        };
    }
    set_close_on_exec(socket_fd);
    suppress_sigpipe(socket_fd);
    const int socket_flags = ::fcntl(socket_fd, F_GETFL, 0);
    if (socket_flags < 0
        || ::fcntl(socket_fd, F_SETFL, socket_flags | O_NONBLOCK) != 0)
    {
        const int code = errno;
        ::close(socket_fd);
        return {
            .ok = false,
            .error = posix_error(TransportStage::EndpointConfigure, code,
                FailureClass::EndpointUnavailable,
                "Unable to configure the control socket."),
        };
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (endpoint.size() >= sizeof(address.sun_path))
    {
        ::close(socket_fd);
        return {
            .ok = false,
            .error = posix_error(TransportStage::EndpointPrepare,
                ENAMETOOLONG, FailureClass::EndpointUnavailable,
                "Control socket path is too long."),
        };
    }
    std::memcpy(address.sun_path, endpoint.data(), endpoint.size());
    address.sun_path[endpoint.size()] = '\0';
    bool connected = ::connect(socket_fd,
                         reinterpret_cast<sockaddr*>(&address),
                         sizeof(address))
        == 0;
    TransportStatus connect_status = TransportStatus::success();
    if (!connected && errno == EINPROGRESS)
    {
        connect_status = wait_for_socket(socket_fd, POLLOUT, deadline,
            TransportStage::ConnectWait);
        if (connect_status.ok)
        {
            int socket_error = 0;
            socklen_t socket_error_size = sizeof(socket_error);
            connected = ::getsockopt(socket_fd, SOL_SOCKET, SO_ERROR,
                            &socket_error, &socket_error_size)
                    == 0
                && socket_error == 0;
            if (!connected)
            {
                const int code = socket_error != 0 ? socket_error : errno;
                connect_status = TransportStatus::failure(posix_error(
                    TransportStage::Connect, code, FailureClass::IoError,
                    "Control request failed."));
            }
        }
    }
    else if (!connected)
    {
        connect_status = TransportStatus::failure(posix_error(
            TransportStage::Connect, errno, FailureClass::IoError,
            "Control request failed."));
    }
    if (!connected)
    {
        ::close(socket_fd);
        return { .ok = false, .error = std::move(connect_status.error) };
    }

    auto write_status = write_control_frame(
        [socket_fd, deadline](const void* data, size_t size,
            TransportStage stage) {
            return client_write_exact(
                socket_fd, data, size, deadline, stage);
        },
        request_bytes);
    std::string response;
    TransportStatus read_status = TransportStatus::failure(write_status.error);
    if (write_status.ok)
    {
        read_status = read_control_frame(
            [socket_fd, deadline](void* data, size_t size,
                TransportStage stage) {
                return client_read_exact(
                    socket_fd, data, size, deadline, stage);
            },
            response);
    }
    ::close(socket_fd);
    if (!write_status.ok)
        return { .ok = false, .error = std::move(write_status.error) };
    if (!read_status.ok)
        return { .ok = false, .error = std::move(read_status.error) };
    return { .ok = true, .response_bytes = std::move(response) };
}

std::unique_ptr<ServerTransport> make_server_transport(
    const ListenerCreateTestHooks*)
{
    return std::make_unique<PosixServerTransport>();
}

} // namespace draxul::control_detail
