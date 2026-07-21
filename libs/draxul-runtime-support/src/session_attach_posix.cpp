#include "session_attach_internal.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace draxul::session_attach_detail
{

namespace
{

std::string errno_message(int error)
{
    return std::strerror(error);
}

sockaddr_un socket_address(const std::string& path)
{
    sockaddr_un address = {};
    address.sun_family = AF_UNIX;
    std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", path.c_str());
    return address;
}

class PosixConnection final : public SessionConnection
{
public:
    explicit PosixConnection(int fd)
        : fd_(fd)
    {
    }

    ~PosixConnection() override
    {
        if (fd_ >= 0)
            ::close(fd_);
    }

    bool read_request(std::string* request, std::string* error) override
    {
        char buffer[kMaxRequestBytes] = {};
        ssize_t bytes_read = 0;
        do
        {
            bytes_read = ::read(fd_, buffer, sizeof(buffer));
        } while (bytes_read < 0 && errno == EINTR);

        if (bytes_read < 0)
        {
            if (error)
                *error = "Failed reading session-attach request: " + errno_message(errno);
            return false;
        }
        if (request)
            request->assign(buffer, static_cast<size_t>(bytes_read));
        return bytes_read > 0;
    }

    bool write_all(std::string_view payload, std::string* error) override
    {
        size_t written = 0;
        while (written < payload.size())
        {
            ssize_t result = 0;
            do
            {
                result = ::write(fd_, payload.data() + written, payload.size() - written);
            } while (result < 0 && errno == EINTR);
            if (result <= 0)
            {
                if (error)
                    *error = "Failed writing session-attach payload: " + errno_message(errno);
                return false;
            }
            written += static_cast<size_t>(result);
        }
        return true;
    }

    bool read_response(std::string* response, std::string* error) override
    {
        if (response)
            response->clear();
        char buffer[256] = {};
        for (;;)
        {
            ssize_t bytes_read = ::read(fd_, buffer, sizeof(buffer));
            if (bytes_read > 0)
            {
                if (response)
                    response->append(buffer, static_cast<size_t>(bytes_read));
                continue;
            }
            if (bytes_read == 0)
                return true;
            if (errno == EINTR)
                continue;
            if (error)
                *error = "Failed reading session-attach response: " + errno_message(errno);
            return false;
        }
    }

private:
    int fd_ = -1;
};

class PosixTransport final : public SessionTransport
{
public:
    explicit PosixTransport(std::string_view session_id)
        : path_((std::filesystem::temp_directory_path()
              / ("draxul-session-attach-" + endpoint_suffix(session_id) + ".sock"))
                    .string())
    {
    }

    ~PosixTransport() override
    {
        close();
    }

    bool start(std::string* error) override
    {
        const sockaddr_un address = socket_address(path_);
        const int probe_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (probe_fd >= 0)
        {
            if (::connect(probe_fd,
                    reinterpret_cast<const sockaddr*>(&address),
                    sizeof(address)) != 0
                && (errno == ENOENT || errno == ECONNREFUSED))
            {
                std::error_code remove_error;
                std::filesystem::remove(path_, remove_error);
            }
            ::close(probe_fd);
        }

        listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (listen_fd_ < 0)
        {
            if (error)
                *error = "Failed to create session-attach socket: " + errno_message(errno);
            return false;
        }
        if (::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
        {
            if (error)
                *error = "Failed to bind session-attach socket: " + errno_message(errno);
            close();
            return false;
        }
        if (::chmod(path_.c_str(), S_IRUSR | S_IWUSR) != 0)
        {
            if (error)
                *error = "Failed to secure session-attach socket: " + errno_message(errno);
            close();
            return false;
        }
        if (::listen(listen_fd_, 4) != 0)
        {
            if (error)
                *error = "Failed to listen on session-attach socket: " + errno_message(errno);
            close();
            return false;
        }
        return true;
    }

    std::unique_ptr<SessionConnection> accept(std::string* error) override
    {
        int client_fd = -1;
        do
        {
            client_fd = ::accept(listen_fd_, nullptr, nullptr);
        } while (client_fd < 0 && errno == EINTR);
        if (client_fd < 0)
        {
            if (error)
                *error = errno_message(errno);
            return {};
        }
        return std::make_unique<PosixConnection>(client_fd);
    }

    TransportResult connect(std::unique_ptr<SessionConnection>* connection,
        std::chrono::milliseconds) override
    {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            return { TransportStatus::Error,
                "Failed creating session-attach client socket: " + errno_message(errno) };

        const sockaddr_un address = socket_address(path_);
        if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
        {
            const int connect_error = errno;
            ::close(fd);
            if (connect_error == ENOENT)
                return { TransportStatus::NoServer, {} };
            return { TransportStatus::Error,
                "Failed connecting to session-attach socket: " + errno_message(connect_error) };
        }

        if (connection)
            *connection = std::make_unique<PosixConnection>(fd);
        else
            ::close(fd);
        return {};
    }

    TransportResult probe(std::chrono::milliseconds timeout) override
    {
        (void)timeout;
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            return { TransportStatus::Error,
                "Failed creating session-attach client socket: " + errno_message(errno) };

        const sockaddr_un address = socket_address(path_);
        if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
        {
            const int connect_error = errno;
            ::close(fd);
            if (connect_error == ENOENT || connect_error == ECONNREFUSED)
                return { TransportStatus::NoServer, {} };
            return { TransportStatus::Error,
                "Failed connecting to session-attach socket: " + errno_message(connect_error) };
        }
        ::close(fd);
        return {};
    }

    void wake(std::string_view request) override
    {
        std::unique_ptr<SessionConnection> connection;
        if (connect(&connection, kClientDeadline).status != TransportStatus::Ok)
            return;
        std::string error;
        (void)connection->write_all(request, &error);
    }

    void close() override
    {
        if (listen_fd_ >= 0)
        {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (!path_.empty())
        {
            std::error_code error;
            std::filesystem::remove(path_, error);
        }
    }

private:
    int listen_fd_ = -1;
    std::string path_;
};

} // namespace

std::unique_ptr<SessionTransport> make_session_transport(std::string_view session_id)
{
    return std::make_unique<PosixTransport>(session_id.empty() ? "default" : session_id);
}

} // namespace draxul::session_attach_detail
