#include <draxul/control_plane.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstdint>
#include <deque>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace draxul
{

namespace
{

constexpr auto kIoTimeout = std::chrono::seconds(5);

uint64_t fnv1a(std::string_view text)
{
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : text)
    {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string session_key(std::string_view session_id)
{
    std::string slug;
    for (unsigned char ch : session_id)
    {
        if (std::isalnum(ch) || ch == '-' || ch == '_')
            slug.push_back(static_cast<char>(ch));
        else
            slug.push_back('_');
        if (slug.size() == 32)
            break;
    }
    if (slug.empty())
        slug = "default";

    std::ostringstream out;
    out << std::hex << fnv1a(session_id) << "-" << slug;
    return out.str();
}

std::string random_token()
{
    std::random_device random;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int i = 0; i < 32; ++i)
        out << std::setw(2) << (random() & 0xff);
    return out.str();
}

bool depth_within_limit(std::string_view text)
{
    size_t depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (char ch : text)
    {
        if (in_string)
        {
            if (escaped)
                escaped = false;
            else if (ch == '\\')
                escaped = true;
            else if (ch == '"')
                in_string = false;
            continue;
        }
        if (ch == '"')
        {
            in_string = true;
            continue;
        }
        if (ch == '{' || ch == '[')
        {
            if (++depth > kControlMaxJsonDepth)
                return false;
        }
        else if (ch == '}' || ch == ']')
        {
            if (depth == 0)
                return false;
            --depth;
        }
    }
    return !in_string && depth == 0;
}

std::array<uint8_t, 4> frame_prefix(size_t size)
{
    const uint32_t value = static_cast<uint32_t>(size);
    return {
        static_cast<uint8_t>(value & 0xff),
        static_cast<uint8_t>((value >> 8) & 0xff),
        static_cast<uint8_t>((value >> 16) & 0xff),
        static_cast<uint8_t>((value >> 24) & 0xff),
    };
}

size_t frame_size(const std::array<uint8_t, 4>& prefix)
{
    return static_cast<size_t>(prefix[0])
        | (static_cast<size_t>(prefix[1]) << 8)
        | (static_cast<size_t>(prefix[2]) << 16)
        | (static_cast<size_t>(prefix[3]) << 24);
}

nlohmann::json response_json(
    std::string_view id, const ControlMethodResult& result)
{
    nlohmann::json response = {
        { "version", kControlProtocolVersion },
        { "id", id },
        { "ok", result.ok },
    };
    if (result.ok)
        response["result"] = result.value;
    else
    {
        response["error"] = {
            { "code", result.error_code },
            { "message", result.error_message },
        };
    }
    return response;
}

ControlMethodResult parse_request(std::string_view bytes,
    std::string_view expected_token,
    ControlRequest& request)
{
    if (bytes.empty() || bytes.size() > kControlMaxMessageBytes
        || !depth_within_limit(bytes))
    {
        return ControlMethodResult::error(
            "invalid_message", "Control message is empty or exceeds structural limits.");
    }

    const nlohmann::json envelope =
        nlohmann::json::parse(bytes, nullptr, false, true);
    if (envelope.is_discarded() || !envelope.is_object())
        return ControlMethodResult::error("invalid_json", "Control message is not valid JSON.");
    if (envelope.value("version", 0) != kControlProtocolVersion)
        return ControlMethodResult::error(
            "unsupported_version", "Unsupported control protocol version.");
    if (!envelope.contains("token") || !envelope["token"].is_string()
        || envelope["token"].get_ref<const std::string&>() != expected_token)
    {
        return ControlMethodResult::error("authentication_failed", "Authentication failed.");
    }
    if (!envelope.contains("id") || !envelope["id"].is_string()
        || envelope["id"].get_ref<const std::string&>().empty()
        || envelope["id"].get_ref<const std::string&>().size() > 64)
    {
        return ControlMethodResult::error("invalid_request", "Request id is invalid.");
    }
    // Preserve the correlation id for all subsequent validation errors so
    // clients receive the actual protocol error instead of an id mismatch.
    request.id = envelope["id"].get<std::string>();
    if (!envelope.contains("method") || !envelope["method"].is_string()
        || envelope["method"].get_ref<const std::string&>().empty()
        || envelope["method"].get_ref<const std::string&>().size() > 64)
    {
        return ControlMethodResult::error("invalid_request", "Method name is invalid.");
    }
    request.method = envelope["method"].get<std::string>();
    if (envelope.contains("params") && !envelope["params"].is_object())
        return ControlMethodResult::error("invalid_request", "Request params must be an object.");

    request.params = envelope.value("params", nlohmann::json::object());
    return ControlMethodResult::success(nullptr);
}

bool write_owner_only_file(
    const std::filesystem::path& path, std::string_view contents, std::string& error)
{
#ifdef _WIN32
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GA;;;SY)(A;;GA;;;OW)",
            SDDL_REVISION_1, &descriptor, nullptr))
    {
        error = "Unable to create control metadata security descriptor.";
        return false;
    }
    SECURITY_ATTRIBUTES attributes{
        sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE
    };
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, &attributes,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    LocalFree(descriptor);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = "Unable to create control metadata file.";
        return false;
    }
    DWORD written = 0;
    const bool ok = contents.size() <= MAXDWORD
        && WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()),
            &written, nullptr)
        && written == contents.size()
        && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok)
        error = "Unable to write control metadata file.";
    return ok;
#else
    const int fd = ::open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0)
    {
        error = "Unable to create control metadata file.";
        return false;
    }
    size_t offset = 0;
    while (offset < contents.size())
    {
        const ssize_t written =
            ::write(fd, contents.data() + offset, contents.size() - offset);
        if (written <= 0)
        {
            ::close(fd);
            error = "Unable to write control metadata file.";
            return false;
        }
        offset += static_cast<size_t>(written);
    }
    const bool ok = ::fsync(fd) == 0;
    ::close(fd);
    if (!ok)
        error = "Unable to flush control metadata file.";
    return ok;
#endif
}

bool read_metadata(const std::filesystem::path& path,
    std::string& endpoint, std::string& token, std::string& error)
{
    std::error_code size_error;
    const auto size = std::filesystem::file_size(path, size_error);
    if (size_error || size == 0 || size > 16 * 1024)
    {
        error = "No usable control endpoint metadata for this Session.";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    const auto metadata = nlohmann::json::parse(bytes, nullptr, false);
    if (metadata.is_discarded() || !metadata.is_object()
        || metadata.value("version", 0) != kControlProtocolVersion
        || !metadata.contains("endpoint") || !metadata["endpoint"].is_string()
        || !metadata.contains("token") || !metadata["token"].is_string())
    {
        error = "Control endpoint metadata is invalid.";
        return false;
    }
    endpoint = metadata["endpoint"].get<std::string>();
    token = metadata["token"].get<std::string>();
    if (endpoint.empty() || token.size() != 64)
    {
        error = "Control endpoint metadata is invalid.";
        return false;
    }
    return true;
}

#ifdef _WIN32

bool read_exact(HANDLE handle, void* data, size_t size)
{
    size_t offset = 0;
    while (offset < size)
    {
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent)
            return false;
        DWORD read = 0;
        BOOL ok = ReadFile(handle, static_cast<char*>(data) + offset,
            static_cast<DWORD>(size - offset), &read, &overlapped);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
        {
            ok = WaitForSingleObject(overlapped.hEvent,
                     static_cast<DWORD>(kIoTimeout.count() * 1000))
                    == WAIT_OBJECT_0
                && GetOverlappedResult(handle, &overlapped, &read, FALSE);
        }
        CloseHandle(overlapped.hEvent);
        if (!ok || read == 0)
            return false;
        offset += read;
    }
    return true;
}

bool write_exact(HANDLE handle, const void* data, size_t size)
{
    size_t offset = 0;
    while (offset < size)
    {
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent)
            return false;
        DWORD written = 0;
        BOOL ok = WriteFile(handle, static_cast<const char*>(data) + offset,
            static_cast<DWORD>(size - offset), &written, &overlapped);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
        {
            ok = WaitForSingleObject(overlapped.hEvent,
                     static_cast<DWORD>(kIoTimeout.count() * 1000))
                    == WAIT_OBJECT_0
                && GetOverlappedResult(handle, &overlapped, &written, FALSE);
        }
        CloseHandle(overlapped.hEvent);
        if (!ok || written == 0)
            return false;
        offset += written;
    }
    return true;
}

#else

bool read_exact(int fd, void* data, size_t size)
{
    size_t offset = 0;
    while (offset < size)
    {
        const ssize_t read = ::recv(fd, static_cast<char*>(data) + offset,
            size - offset, 0);
        if (read <= 0)
            return false;
        offset += static_cast<size_t>(read);
    }
    return true;
}

bool write_exact(int fd, const void* data, size_t size)
{
    size_t offset = 0;
    while (offset < size)
    {
        const ssize_t written = ::send(fd,
            static_cast<const char*>(data) + offset, size - offset, 0);
        if (written <= 0)
            return false;
        offset += static_cast<size_t>(written);
    }
    return true;
}

#endif

template <typename Handle>
bool read_frame(Handle handle, std::string& bytes)
{
    std::array<uint8_t, 4> prefix{};
    if (!read_exact(handle, prefix.data(), prefix.size()))
        return false;
    const size_t size = frame_size(prefix);
    if (size == 0 || size > kControlMaxMessageBytes)
        return false;
    bytes.resize(size);
    return read_exact(handle, bytes.data(), bytes.size());
}

template <typename Handle>
bool write_frame(Handle handle, std::string_view bytes)
{
    if (bytes.empty() || bytes.size() > kControlMaxMessageBytes)
        return false;
    const auto prefix = frame_prefix(bytes.size());
    return write_exact(handle, prefix.data(), prefix.size())
        && write_exact(handle, bytes.data(), bytes.size());
}

} // namespace

ControlMethodResult ControlMethodResult::success(nlohmann::json result)
{
    return { true, std::move(result), {}, {} };
}

ControlMethodResult ControlMethodResult::error(
    std::string code, std::string message)
{
    return { false, nullptr, std::move(code), std::move(message) };
}

std::filesystem::path control_runtime_directory(
    const std::filesystem::path& config_directory)
{
    return config_directory / "runtime";
}

std::filesystem::path control_metadata_path(
    const std::filesystem::path& runtime_directory, std::string_view session_id)
{
    return runtime_directory / (session_key(session_id) + ".control.json");
}

class ControlServer::Impl
{
public:
    struct Pending
    {
        ControlRequest request;
        std::promise<ControlMethodResult> response;
    };

    bool start(std::string new_session_id,
        std::filesystem::path new_runtime_directory,
        std::function<void()> wake,
        std::string* error);
    void stop();
    void run(std::stop_token stop_token);
    void process_pending(const Handler& handler);
    ControlMethodResult dispatch(ControlRequest request);

    std::string session_id;
    std::filesystem::path runtime_directory;
    std::filesystem::path metadata;
    std::string endpoint;
    std::string token;
    std::function<void()> wake_main_thread;
    std::jthread thread;
    std::atomic<bool> active = false;
    std::mutex queue_mutex;
    std::deque<std::shared_ptr<Pending>> queue;
};

bool ControlServer::Impl::start(std::string new_session_id,
    std::filesystem::path new_runtime_directory,
    std::function<void()> wake,
    std::string* error)
{
    if (active)
    {
        if (error)
            *error = "Control server is already running.";
        return false;
    }
    session_id = std::move(new_session_id);
    runtime_directory = std::move(new_runtime_directory);
    wake_main_thread = std::move(wake);
    metadata = control_metadata_path(runtime_directory, session_id);
    token = random_token();
#ifdef _WIN32
    endpoint = R"(\\.\pipe\draxul-control-)" + session_key(session_id);
#else
    endpoint = (runtime_directory / (session_key(session_id) + ".sock")).string();
    sockaddr_un endpoint_address{};
    if (endpoint.size() >= sizeof(endpoint_address.sun_path))
    {
        if (error)
            *error = "Control socket path is too long.";
        return false;
    }
#endif

    std::error_code dir_error;
    std::filesystem::create_directories(runtime_directory, dir_error);
    if (dir_error)
    {
        if (error)
            *error = "Unable to create control runtime directory.";
        return false;
    }
#ifndef _WIN32
    ::chmod(runtime_directory.c_str(), 0700);
#endif

    const std::string metadata_bytes = nlohmann::json{
        { "version", kControlProtocolVersion },
        { "session_id", session_id },
        { "endpoint", endpoint },
        { "token", token },
    }.dump();
    std::string write_error;
    if (!write_owner_only_file(metadata, metadata_bytes, write_error))
    {
        if (error)
            *error = std::move(write_error);
        return false;
    }

    active = true;
    thread = std::jthread([this](std::stop_token stop_token) { run(stop_token); });
    return true;
}

void ControlServer::Impl::stop()
{
    if (thread.joinable())
    {
        thread.request_stop();
        thread.join();
    }
    active = false;
    std::error_code ignored;
    std::filesystem::remove(metadata, ignored);
#ifndef _WIN32
    if (!endpoint.empty())
        std::filesystem::remove(endpoint, ignored);
#endif
    std::lock_guard lock(queue_mutex);
    for (auto& pending : queue)
    {
        pending->response.set_value(
            ControlMethodResult::error("server_stopping", "Control server is stopping."));
    }
    queue.clear();
}

ControlMethodResult ControlServer::Impl::dispatch(ControlRequest request)
{
    auto pending = std::make_shared<Pending>();
    pending->request = std::move(request);
    auto response = pending->response.get_future();
    {
        std::lock_guard lock(queue_mutex);
        queue.push_back(pending);
    }
    if (wake_main_thread)
        wake_main_thread();
    if (response.wait_for(kIoTimeout) != std::future_status::ready)
        return ControlMethodResult::error(
            "main_thread_timeout", "Draxul did not process the request in time.");
    return response.get();
}

void ControlServer::Impl::process_pending(const Handler& handler)
{
    std::deque<std::shared_ptr<Pending>> pending;
    {
        std::lock_guard lock(queue_mutex);
        pending.swap(queue);
    }
    for (auto& item : pending)
    {
        ControlMethodResult result;
        try
        {
            result = handler(item->request);
        }
        catch (const std::exception&)
        {
            result = ControlMethodResult::error(
                "internal_error", "The control request failed internally.");
        }
        item->response.set_value(std::move(result));
    }
}

#ifdef _WIN32

void ControlServer::Impl::run(std::stop_token stop_token)
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GA;;;SY)(A;;GA;;;OW)",
            SDDL_REVISION_1, &descriptor, nullptr))
    {
        active = false;
        return;
    }
    SECURITY_ATTRIBUTES attributes{
        sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE
    };
    const std::wstring pipe_name(endpoint.begin(), endpoint.end());

    while (!stop_token.stop_requested())
    {
        HANDLE pipe = CreateNamedPipeW(pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            4, static_cast<DWORD>(kControlMaxMessageBytes),
            static_cast<DWORD>(kControlMaxMessageBytes), 0, &attributes);
        if (pipe == INVALID_HANDLE_VALUE)
            break;

        OVERLAPPED connect{};
        connect.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL connected = ConnectNamedPipe(pipe, &connect);
        if (!connected && GetLastError() == ERROR_PIPE_CONNECTED)
            connected = TRUE;
        else if (!connected && GetLastError() == ERROR_IO_PENDING)
        {
            while (!stop_token.stop_requested())
            {
                const DWORD wait = WaitForSingleObject(connect.hEvent, 100);
                if (wait == WAIT_OBJECT_0)
                {
                    DWORD ignored = 0;
                    connected = GetOverlappedResult(pipe, &connect, &ignored, FALSE);
                    break;
                }
                if (wait != WAIT_TIMEOUT)
                    break;
            }
        }
        CloseHandle(connect.hEvent);
        if (connected && !stop_token.stop_requested())
        {
            std::string bytes;
            ControlRequest request;
            ControlMethodResult result;
            if (!read_frame(pipe, bytes))
                result = ControlMethodResult::error("invalid_frame", "Invalid control frame.");
            else
            {
                result = parse_request(bytes, token, request);
                if (result.ok)
                    result = dispatch(request);
            }
            const std::string response = response_json(request.id, result).dump();
            write_frame(pipe, response);
            FlushFileBuffers(pipe);
            DisconnectNamedPipe(pipe);
        }
        CancelIoEx(pipe, nullptr);
        CloseHandle(pipe);
    }
    LocalFree(descriptor);
    active = false;
}

#else

void ControlServer::Impl::run(std::stop_token stop_token)
{
    const int server = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0)
    {
        active = false;
        return;
    }
    ::unlink(endpoint.c_str());
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, endpoint.c_str(), endpoint.size() + 1);
    if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
        || ::chmod(endpoint.c_str(), 0600) != 0
        || ::listen(server, 4) != 0)
    {
        ::close(server);
        active = false;
        return;
    }
    const int flags = ::fcntl(server, F_GETFL, 0);
    ::fcntl(server, F_SETFL, flags | O_NONBLOCK);

    while (!stop_token.stop_requested())
    {
        pollfd descriptor{ server, POLLIN, 0 };
        const int ready = ::poll(&descriptor, 1, 100);
        if (ready <= 0)
            continue;
        const int client = ::accept(server, nullptr, nullptr);
        if (client < 0)
            continue;
        timeval timeout{
            static_cast<long>(kIoTimeout.count()), 0
        };
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        std::string bytes;
        ControlRequest request;
        ControlMethodResult result;
        if (!read_frame(client, bytes))
            result = ControlMethodResult::error("invalid_frame", "Invalid control frame.");
        else
        {
            result = parse_request(bytes, token, request);
            if (result.ok)
                result = dispatch(request);
        }
        const std::string response = response_json(request.id, result).dump();
        write_frame(client, response);
        ::close(client);
    }
    ::close(server);
    ::unlink(endpoint.c_str());
    active = false;
}

#endif

ControlServer::ControlServer()
    : impl_(std::make_unique<Impl>())
{
}

ControlServer::~ControlServer()
{
    stop();
}

bool ControlServer::start(std::string session_id,
    std::filesystem::path runtime_directory,
    std::function<void()> wake_main_thread,
    std::string* error)
{
    return impl_->start(std::move(session_id), std::move(runtime_directory),
        std::move(wake_main_thread), error);
}

void ControlServer::stop()
{
    impl_->stop();
}

bool ControlServer::running() const
{
    return impl_->active;
}

void ControlServer::process_pending(const Handler& handler)
{
    impl_->process_pending(handler);
}

const std::string& ControlServer::endpoint() const
{
    return impl_->endpoint;
}

const std::filesystem::path& ControlServer::metadata_path() const
{
    return impl_->metadata;
}

ControlClientResult ControlClient::request(std::string_view session_id,
    const std::filesystem::path& runtime_directory,
    std::string_view method,
    nlohmann::json params)
{
    std::string endpoint;
    std::string token;
    std::string metadata_error;
    if (!read_metadata(control_metadata_path(runtime_directory, session_id),
            endpoint, token, metadata_error))
    {
        return { false, nullptr, "endpoint_unavailable", std::move(metadata_error) };
    }

    const std::string id = random_token().substr(0, 16);
    const std::string request = nlohmann::json{
        { "version", kControlProtocolVersion },
        { "token", token },
        { "id", id },
        { "method", method },
        { "params", std::move(params) },
    }.dump();

    std::string response_bytes;
#ifdef _WIN32
    const std::wstring pipe_name(endpoint.begin(), endpoint.end());
    if (!WaitNamedPipeW(pipe_name.c_str(),
            static_cast<DWORD>(kIoTimeout.count() * 1000)))
    {
        return { false, nullptr, "endpoint_unavailable",
            "The Draxul Session control pipe is unavailable." };
    }
    HANDLE pipe = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        return { false, nullptr, "endpoint_unavailable",
            "Unable to connect to the Draxul Session." };
    }
    const bool io_ok = write_frame(pipe, request)
        && read_frame(pipe, response_bytes);
    CloseHandle(pipe);
#else
    const int socket_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0)
        return { false, nullptr, "endpoint_unavailable", "Unable to create control socket." };
    timeval timeout{
        static_cast<long>(kIoTimeout.count()), 0
    };
    ::setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (endpoint.size() >= sizeof(address.sun_path))
    {
        ::close(socket_fd);
        return { false, nullptr, "endpoint_unavailable", "Control socket path is too long." };
    }
    std::memcpy(address.sun_path, endpoint.c_str(), endpoint.size() + 1);
    const bool connected =
        ::connect(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    const bool io_ok = connected && write_frame(socket_fd, request)
        && read_frame(socket_fd, response_bytes);
    ::close(socket_fd);
#endif
    if (!io_ok)
        return { false, nullptr, "io_error", "Control request failed." };
    if (!depth_within_limit(response_bytes))
        return { false, nullptr, "invalid_response",
            "Control response exceeds the JSON nesting limit." };

    const auto response =
        nlohmann::json::parse(response_bytes, nullptr, false, true);
    if (response.is_discarded() || !response.is_object())
        return { false, nullptr, "invalid_response",
            "Control response is not a JSON object." };
    if (response.value("version", 0) != kControlProtocolVersion)
        return { false, nullptr, "invalid_response",
            "Control response has an unsupported version." };
    if (response.value("id", std::string{}) != id)
        return { false, nullptr, "invalid_response",
            "Control response does not match the request id." };
    if (!response.contains("ok") || !response["ok"].is_boolean())
        return { false, nullptr, "invalid_response",
            "Control response has no valid result discriminator." };
    if (response["ok"].get<bool>())
        return { true, response.value("result", nlohmann::json{}), {}, {} };
    if (!response.contains("error") || !response["error"].is_object())
        return { false, nullptr, "invalid_response", "Control response is invalid." };
    return {
        false,
        nullptr,
        response["error"].value("code", "unknown_error"),
        response["error"].value("message", "Control request failed."),
    };
}

} // namespace draxul
