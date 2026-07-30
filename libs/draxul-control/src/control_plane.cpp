#include <draxul/control_plane.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// sddl.h depends on Windows API family declarations from windows.h.
// clang-format off
#include <windows.h>
#include <sddl.h>
// clang-format on
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
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

std::string normalized_runtime_key(
    const std::filesystem::path& runtime_directory)
{
    std::error_code path_error;
    auto normalized = std::filesystem::weakly_canonical(
        runtime_directory, path_error);
    if (path_error)
    {
        path_error.clear();
        normalized = std::filesystem::absolute(
            runtime_directory, path_error);
        if (path_error)
            normalized = runtime_directory;
    }
    std::string value = normalized.lexically_normal().generic_string();
#ifdef _WIN32
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
#endif
    return value;
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

std::string dump_wire_json(const nlohmann::json& value)
{
    // Terminal cell text ultimately comes from arbitrary PTY bytes. The
    // terminal core sanitizes it, but replacement mode keeps a malformed
    // payload from escaping a subsystem and terminating a listener thread.
    return value.dump(-1, ' ', false,
        nlohmann::detail::error_handler_t::replace);
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

    const nlohmann::json envelope = nlohmann::json::parse(bytes, nullptr, false, true);
    if (envelope.is_discarded() || !envelope.is_object())
        return ControlMethodResult::error("invalid_json", "Control message is not valid JSON.");
    if (envelope.value("version", 0) != kControlProtocolVersion)
        return ControlMethodResult::error(
            "unsupported_version", "Unsupported control protocol version.");
    if (!envelope.contains("id") || !envelope["id"].is_string()
        || envelope["id"].get_ref<const std::string&>().empty()
        || envelope["id"].get_ref<const std::string&>().size() > 64)
    {
        return ControlMethodResult::error("invalid_request", "Request id is invalid.");
    }
    // Preserve the correlation id even for authentication failures. This lets
    // a client distinguish a stale cached token after a server restart and
    // safely retry after re-reading owner-only endpoint metadata.
    request.id = envelope["id"].get<std::string>();
    if (!envelope.contains("token") || !envelope["token"].is_string()
        || envelope["token"].get_ref<const std::string&>() != expected_token)
    {
        return ControlMethodResult::error("authentication_failed", "Authentication failed.");
    }
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
    if (envelope.contains("timeout_ms"))
    {
        if (!envelope["timeout_ms"].is_number_integer())
        {
            return ControlMethodResult::error(
                "invalid_request", "Request timeout is invalid.");
        }
        const int64_t timeout_ms
            = envelope["timeout_ms"].get<int64_t>();
        constexpr int64_t maximum_timeout_ms
            = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::hours(1))
                  .count();
        if (timeout_ms <= 0
            || timeout_ms > maximum_timeout_ms)
        {
            return ControlMethodResult::error(
                "invalid_request", "Request timeout is out of range.");
        }
        request.expires_at = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(timeout_ms);
    }
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
        const ssize_t written = ::write(fd, contents.data() + offset, contents.size() - offset);
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

struct CachedMetadata
{
    std::string endpoint;
    std::string token;
    std::chrono::steady_clock::time_point expires_at;
};

std::mutex& metadata_cache_mutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, CachedMetadata>& metadata_cache()
{
    static std::unordered_map<std::string, CachedMetadata> cache;
    return cache;
}

bool read_cached_metadata(const std::filesystem::path& path,
    std::string& endpoint, std::string& token, std::string& error)
{
    const std::string key = path.lexically_normal().generic_string();
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard guard(metadata_cache_mutex());
        const auto found = metadata_cache().find(key);
        if (found != metadata_cache().end()
            && found->second.expires_at > now)
        {
            endpoint = found->second.endpoint;
            token = found->second.token;
            return true;
        }
    }
    if (!read_metadata(path, endpoint, token, error))
        return false;
    {
        std::lock_guard guard(metadata_cache_mutex());
        metadata_cache()[key] = {
            .endpoint = endpoint,
            .token = token,
            .expires_at = now + std::chrono::seconds(1),
        };
    }
    return true;
}

void invalidate_cached_metadata(const std::filesystem::path& path)
{
    const std::string key = path.lexically_normal().generic_string();
    std::lock_guard guard(metadata_cache_mutex());
    metadata_cache().erase(key);
}

#ifdef _WIN32

bool await_overlapped_io(HANDLE handle, OVERLAPPED& overlapped,
    DWORD& transferred, DWORD initial_error)
{
    if (initial_error != ERROR_IO_PENDING)
    {
        SetLastError(initial_error);
        return false;
    }
    const DWORD wait = WaitForSingleObject(overlapped.hEvent,
        static_cast<DWORD>(kIoTimeout.count() * 1000));
    if (wait == WAIT_OBJECT_0)
        return GetOverlappedResult(
            handle, &overlapped, &transferred, FALSE);

    // The OVERLAPPED structure and caller's data buffer must outlive the I/O.
    // Returning directly on timeout left both stack objects available for
    // reuse while the kernel could still complete the pending operation.
    const DWORD error
        = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
    CancelIoEx(handle, &overlapped);
    WaitForSingleObject(overlapped.hEvent, INFINITE);
    DWORD ignored = 0;
    GetOverlappedResult(handle, &overlapped, &ignored, FALSE);
    SetLastError(error);
    return false;
}

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
        if (!ok)
            ok = await_overlapped_io(
                handle, overlapped, read, GetLastError());
        const DWORD io_error = ok ? ERROR_SUCCESS : GetLastError();
        CloseHandle(overlapped.hEvent);
        if (!ok || read == 0)
        {
            SetLastError(ok ? ERROR_BROKEN_PIPE : io_error);
            return false;
        }
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
        if (!ok)
            ok = await_overlapped_io(
                handle, overlapped, written, GetLastError());
        const DWORD io_error = ok ? ERROR_SUCCESS : GetLastError();
        CloseHandle(overlapped.hEvent);
        if (!ok || written == 0)
        {
            SetLastError(ok ? ERROR_BROKEN_PIPE : io_error);
            return false;
        }
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

using ControlDeadline = std::chrono::steady_clock::time_point;

std::chrono::milliseconds remaining_time(ControlDeadline deadline)
{
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline)
        return std::chrono::milliseconds(0);
    return std::max(std::chrono::milliseconds(1),
        std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now));
}

#ifdef _WIN32

bool await_client_io(HANDLE handle, OVERLAPPED& overlapped,
    DWORD& transferred, DWORD initial_error,
    ControlDeadline deadline)
{
    if (initial_error != ERROR_IO_PENDING)
    {
        SetLastError(initial_error);
        return false;
    }
    const auto remaining = remaining_time(deadline);
    const DWORD wait = WaitForSingleObject(overlapped.hEvent,
        static_cast<DWORD>(remaining.count()));
    if (wait == WAIT_OBJECT_0)
        return GetOverlappedResult(
            handle, &overlapped, &transferred, FALSE);

    const DWORD error
        = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
    CancelIoEx(handle, &overlapped);
    WaitForSingleObject(overlapped.hEvent, INFINITE);
    DWORD ignored = 0;
    GetOverlappedResult(handle, &overlapped, &ignored, FALSE);
    SetLastError(error);
    return false;
}

bool client_read_exact(HANDLE handle, void* data, size_t size,
    ControlDeadline deadline)
{
    size_t offset = 0;
    while (offset < size)
    {
        if (remaining_time(deadline).count() == 0)
        {
            SetLastError(ERROR_TIMEOUT);
            return false;
        }
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent)
            return false;
        DWORD read = 0;
        BOOL ok = ReadFile(handle, static_cast<char*>(data) + offset,
            static_cast<DWORD>(size - offset), &read, &overlapped);
        if (!ok)
        {
            ok = await_client_io(
                handle, overlapped, read, GetLastError(), deadline);
        }
        const DWORD io_error = ok ? ERROR_SUCCESS : GetLastError();
        CloseHandle(overlapped.hEvent);
        if (!ok || read == 0)
        {
            SetLastError(ok ? ERROR_BROKEN_PIPE : io_error);
            return false;
        }
        offset += read;
    }
    return true;
}

bool client_write_exact(HANDLE handle, const void* data, size_t size,
    ControlDeadline deadline)
{
    size_t offset = 0;
    while (offset < size)
    {
        if (remaining_time(deadline).count() == 0)
        {
            SetLastError(ERROR_TIMEOUT);
            return false;
        }
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent)
            return false;
        DWORD written = 0;
        BOOL ok = WriteFile(handle,
            static_cast<const char*>(data) + offset,
            static_cast<DWORD>(size - offset), &written, &overlapped);
        if (!ok)
        {
            ok = await_client_io(handle, overlapped, written,
                GetLastError(), deadline);
        }
        const DWORD io_error = ok ? ERROR_SUCCESS : GetLastError();
        CloseHandle(overlapped.hEvent);
        if (!ok || written == 0)
        {
            SetLastError(ok ? ERROR_BROKEN_PIPE : io_error);
            return false;
        }
        offset += written;
    }
    return true;
}

#else

bool wait_for_socket(
    int fd, short events, ControlDeadline deadline)
{
    while (true)
    {
        const auto remaining = remaining_time(deadline);
        if (remaining.count() == 0)
        {
            errno = ETIMEDOUT;
            return false;
        }
        pollfd descriptor{ fd, events, 0 };
        const int ready = ::poll(&descriptor, 1,
            static_cast<int>(remaining.count()));
        if (ready > 0)
            return (descriptor.revents
                       & (events | POLLERR | POLLHUP))
                != 0;
        if (ready == 0)
        {
            errno = ETIMEDOUT;
            return false;
        }
        if (errno != EINTR)
            return false;
    }
}

bool client_read_exact(int fd, void* data, size_t size,
    ControlDeadline deadline)
{
    size_t offset = 0;
    while (offset < size)
    {
        if (!wait_for_socket(fd, POLLIN, deadline))
            return false;
        const ssize_t read = ::recv(fd,
            static_cast<char*>(data) + offset,
            size - offset, 0);
        if (read > 0)
        {
            offset += static_cast<size_t>(read);
            continue;
        }
        if (read < 0 && (errno == EINTR
                            || errno == EAGAIN
                            || errno == EWOULDBLOCK))
        {
            continue;
        }
        return false;
    }
    return true;
}

bool client_write_exact(int fd, const void* data, size_t size,
    ControlDeadline deadline)
{
    size_t offset = 0;
    while (offset < size)
    {
        if (!wait_for_socket(fd, POLLOUT, deadline))
            return false;
        const ssize_t written = ::send(fd,
            static_cast<const char*>(data) + offset,
            size - offset, 0);
        if (written > 0)
        {
            offset += static_cast<size_t>(written);
            continue;
        }
        if (written < 0 && (errno == EINTR
                               || errno == EAGAIN
                               || errno == EWOULDBLOCK))
        {
            continue;
        }
        return false;
    }
    return true;
}

#endif

template <typename Handle>
bool client_read_frame(Handle handle, std::string& bytes,
    ControlDeadline deadline)
{
    std::array<uint8_t, 4> prefix{};
    if (!client_read_exact(
            handle, prefix.data(), prefix.size(), deadline))
    {
        return false;
    }
    const size_t size = frame_size(prefix);
    if (size == 0 || size > kControlMaxMessageBytes)
        return false;
    bytes.resize(size);
    return client_read_exact(
        handle, bytes.data(), bytes.size(), deadline);
}

template <typename Handle>
bool client_write_frame(Handle handle, std::string_view bytes,
    ControlDeadline deadline)
{
    if (bytes.empty() || bytes.size() > kControlMaxMessageBytes)
        return false;
    const auto prefix = frame_prefix(bytes.size());
    return client_write_exact(
               handle, prefix.data(), prefix.size(), deadline)
        && client_write_exact(
            handle, bytes.data(), bytes.size(), deadline);
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
        std::atomic<bool> completed = false;
        std::atomic<bool> cancelled = false;

        bool complete(ControlMethodResult result)
        {
            bool expected = false;
            if (!completed.compare_exchange_strong(expected, true))
                return false;
            response.set_value(std::move(result));
            return true;
        }
    };

    bool start(std::string new_session_id,
        std::filesystem::path new_runtime_directory,
        std::function<void()> wake,
        std::string* error,
        nlohmann::json metadata_extra);
    void stop();
    void run(std::stop_token stop_token);
    void process_pending(const Handler& handler);
    ControlMethodResult dispatch(ControlRequest request);
    void complete_pending(const std::shared_ptr<Pending>& pending,
        ControlMethodResult result);
#ifndef _WIN32
    bool acquire_endpoint_lock(std::string* error);
    void release_endpoint_lock();
#endif
    // The listener is only established on the worker thread, so start() waits
    // for this before reporting. Empty string = listening; non-empty = the
    // reason it could not. First report wins; later calls are ignored.
    void report_startup(std::string result);

    std::string session_id;
    std::filesystem::path runtime_directory;
    std::filesystem::path metadata;
    std::string endpoint;
    std::string token;
    std::function<void()> wake_main_thread;
    std::jthread thread;
    std::atomic<bool> active = false;
    std::atomic<bool> stopping = false;
    std::mutex queue_mutex;
    std::deque<std::shared_ptr<Pending>> queue;
    std::unordered_set<std::shared_ptr<Pending>> pending_requests;
    std::mutex startup_mutex;
    std::condition_variable startup_changed;
    std::optional<std::string> startup_result;
    bool owns_endpoint = false;
    std::atomic<bool> endpoint_in_use = false;
    std::atomic<uint32_t> listener_error = 0;
#ifndef _WIN32
    std::filesystem::path endpoint_lock_path;
    int endpoint_lock = -1;
#endif
};

void ControlServer::Impl::report_startup(std::string result)
{
    {
        std::lock_guard<std::mutex> guard(startup_mutex);
        if (startup_result)
            return;
        startup_result = std::move(result);
    }
    startup_changed.notify_all();
}

#ifndef _WIN32
bool ControlServer::Impl::acquire_endpoint_lock(std::string* error)
{
    endpoint_lock_path
        = runtime_directory / (session_key(session_id) + ".control.lock");

    int descriptor = -1;
    do
    {
        descriptor = ::open(endpoint_lock_path.c_str(),
            O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    } while (descriptor < 0 && errno == EINTR);

    if (descriptor < 0 && errno == EEXIST)
    {
        do
        {
            descriptor = ::open(
                endpoint_lock_path.c_str(), O_RDWR | O_CLOEXEC);
        } while (descriptor < 0 && errno == EINTR);
    }
    if (descriptor < 0)
    {
        if (error)
            *error = "Unable to open the control endpoint lock.";
        return false;
    }

    // The lock file is deliberately persistent. Removing it during shutdown
    // would let a contender lock a newly-created inode while the incumbent
    // still held the old one. A crashed process releases flock automatically,
    // so the next launcher can safely reuse the same file.
    ::fchmod(descriptor, 0600);
    int lock_result = -1;
    do
    {
        lock_result = ::flock(descriptor, LOCK_EX | LOCK_NB);
    } while (lock_result != 0 && errno == EINTR);
    if (lock_result != 0)
    {
        const int lock_error = errno;
        ::close(descriptor);
        const bool taken = lock_error == EWOULDBLOCK
            || lock_error == EAGAIN;
        endpoint_in_use = taken;
        if (error)
        {
            *error = taken
                ? "Control endpoint is already in use by another Draxul instance."
                : "Unable to lock the control endpoint.";
        }
        return false;
    }

    endpoint_lock = descriptor;
    return true;
}

void ControlServer::Impl::release_endpoint_lock()
{
    if (endpoint_lock < 0)
        return;
    ::flock(endpoint_lock, LOCK_UN);
    ::close(endpoint_lock);
    endpoint_lock = -1;
}
#endif

bool ControlServer::Impl::start(std::string new_session_id,
    std::filesystem::path new_runtime_directory,
    std::function<void()> wake,
    std::string* error,
    nlohmann::json metadata_extra)
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

    endpoint_in_use = false;
    listener_error = 0;
    owns_endpoint = false;
    stopping = false;
#ifndef _WIN32
    if (!acquire_endpoint_lock(error))
        return false;
#endif

    active = true;
    {
        std::lock_guard<std::mutex> guard(startup_mutex);
        startup_result.reset();
    }
    thread = std::jthread([this](std::stop_token stop_token) { run(stop_token); });

    // Wait for the worker to actually claim the endpoint. Returning true
    // before bind()/CreateNamedPipeW meant a failed listener was silent and
    // the app ran on with a dead control plane.
    std::string startup_error;
    {
        std::unique_lock<std::mutex> lock(startup_mutex);
        startup_changed.wait(lock, [this] { return startup_result.has_value(); });
        startup_error = *startup_result;
    }
    if (!startup_error.empty())
    {
        if (error)
            *error = startup_error;
        // The endpoint was never ours: join the worker without touching the
        // other instance's socket or metadata (owns_endpoint is still false).
        stop();
        return false;
    }
    owns_endpoint = true;

    // Publish the token only now. Writing it before the listener was claimed
    // meant a second instance overwrote a live server's credentials, and every
    // CLI request then authenticated against the wrong process.
    if (!metadata_extra.is_object())
        metadata_extra = nlohmann::json::object();
    metadata_extra["version"] = kControlProtocolVersion;
    metadata_extra["session_id"] = session_id;
    metadata_extra["endpoint"] = endpoint;
    metadata_extra["token"] = token;
    const std::string metadata_bytes = metadata_extra.dump();
    std::string write_error;
    if (!write_owner_only_file(metadata, metadata_bytes, write_error))
    {
        if (error)
            *error = std::move(write_error);
        stop();
        return false;
    }
    return true;
}

void ControlServer::Impl::stop()
{
    stopping = true;
    std::vector<std::shared_ptr<Pending>> pending;
    {
        std::lock_guard lock(queue_mutex);
        queue.clear();
        pending.reserve(pending_requests.size());
        for (const auto& request : pending_requests)
            pending.push_back(request);
        pending_requests.clear();
    }
    // Dispatch waits happen on listener threads. Complete every outstanding
    // promise before joining those listeners, while the main-loop handler is
    // still guaranteed not to receive another queued request.
    for (const auto& request : pending)
    {
        request->complete(ControlMethodResult::error(
            "server_stopping", "Control server is stopping."));
    }

    if (thread.joinable())
    {
        thread.request_stop();
        thread.join();
    }
    active = false;
    // Only tear down what this process actually claimed. A start() that lost
    // the endpoint to another live instance must never remove that instance's
    // socket or overwrite its metadata.
    if (owns_endpoint)
    {
        std::error_code ignored;
        std::filesystem::remove(metadata, ignored);
#ifndef _WIN32
        if (!endpoint.empty())
            std::filesystem::remove(endpoint, ignored);
#endif
        owns_endpoint = false;
    }
#ifndef _WIN32
    release_endpoint_lock();
#endif
}

ControlMethodResult ControlServer::Impl::dispatch(ControlRequest request)
{
    if (stopping)
    {
        return ControlMethodResult::error(
            "server_stopping", "Control server is stopping.");
    }
    auto pending = std::make_shared<Pending>();
    pending->request = std::move(request);
    if (std::chrono::steady_clock::now()
        >= pending->request.expires_at)
    {
        pending->cancelled = true;
        return ControlMethodResult::error(
            "deadline_exceeded",
            "The control request expired before dispatch.");
    }
    auto response = pending->response.get_future();
    {
        std::lock_guard lock(queue_mutex);
        if (stopping)
        {
            return ControlMethodResult::error(
                "server_stopping", "Control server is stopping.");
        }
        queue.push_back(pending);
        pending_requests.insert(pending);
    }
    if (wake_main_thread)
        wake_main_thread();
    auto wait_budget
        = std::chrono::duration_cast<std::chrono::milliseconds>(
            kIoTimeout);
    if (pending->request.expires_at
        != std::chrono::steady_clock::time_point::max())
    {
        wait_budget = std::min(wait_budget,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                pending->request.expires_at
                - std::chrono::steady_clock::now()));
    }
    if (wait_budget <= std::chrono::milliseconds::zero()
        || response.wait_for(wait_budget)
            != std::future_status::ready)
    {
        pending->cancelled = true;
        auto timeout = ControlMethodResult::error(
            "main_thread_timeout", "Draxul did not process the request in time.");
        complete_pending(pending, timeout);
        return timeout;
    }
    return response.get();
}

void ControlServer::Impl::complete_pending(
    const std::shared_ptr<Pending>& pending, ControlMethodResult result)
{
    pending->complete(std::move(result));
    std::lock_guard lock(queue_mutex);
    pending_requests.erase(pending);
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
        if (item->cancelled
            || std::chrono::steady_clock::now()
                >= item->request.expires_at)
        {
            item->cancelled = true;
            complete_pending(item, ControlMethodResult::error(
                "main_thread_timeout",
                "The timed-out control request was cancelled."));
            continue;
        }
        if (stopping || item->completed)
        {
            complete_pending(item, ControlMethodResult::error(
                "server_stopping", "Control server is stopping."));
            continue;
        }
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
        complete_pending(item, std::move(result));
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
        report_startup("Unable to build the control pipe security descriptor.");
        active = false;
        return;
    }
    SECURITY_ATTRIBUTES attributes{
        sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE
    };
    const std::wstring pipe_name(endpoint.begin(), endpoint.end());
    // Claim the name before starting the listener pool. Without
    // FILE_FLAG_FIRST_PIPE_INSTANCE, a second Draxul process can quietly add
    // an instance with the same name and race this process for clients.
    HANDLE initial_pipe = CreateNamedPipeW(pipe_name.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED
            | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        4, static_cast<DWORD>(kControlMaxMessageBytes),
        static_cast<DWORD>(kControlMaxMessageBytes), 0, &attributes);
    if (initial_pipe == INVALID_HANDLE_VALUE)
    {
        const DWORD create_error = GetLastError();
        // ERROR_PIPE_BUSY is returned when the incumbent has already created
        // every advertised listener instance; it is the same ownership
        // outcome as ERROR_ACCESS_DENIED from FIRST_PIPE_INSTANCE.
        const bool taken = create_error == ERROR_ACCESS_DENIED
            || create_error == ERROR_PIPE_BUSY;
        endpoint_in_use = taken;
        report_startup(taken
                ? "Control endpoint is already in use by another Draxul instance."
                : "Unable to create the control pipe.");
        LocalFree(descriptor);
        active = false;
        return;
    }
    report_startup({});

    // A remote terminal client holds its pipe instance while the request is
    // dispatched to the server's main loop and the response is transferred.
    // Keeping only one live instance therefore lets a slow poll or a stalled
    // client starve every other UI. Four independent listeners match the pipe's
    // advertised instance count and keep observers responsive while another
    // client is resizing, polling, or disconnecting.
    auto serve_connections
        = [this, &attributes, &pipe_name](std::stop_token shared_stop,
              HANDLE first_pipe) {
              HANDLE pipe = first_pipe;
              while (!shared_stop.stop_requested())
              {
                  if (pipe == INVALID_HANDLE_VALUE)
                  {
                      pipe = CreateNamedPipeW(pipe_name.c_str(),
                          PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                          4, static_cast<DWORD>(kControlMaxMessageBytes),
                          static_cast<DWORD>(kControlMaxMessageBytes),
                          0, &attributes);
                      if (pipe == INVALID_HANDLE_VALUE)
                      {
                          listener_error = GetLastError();
                          std::this_thread::sleep_for(
                              std::chrono::milliseconds(25));
                          continue;
                      }
                  }

                  OVERLAPPED connect{};
                  connect.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                  BOOL connected = ConnectNamedPipe(pipe, &connect);
                  if (!connected && GetLastError() == ERROR_PIPE_CONNECTED)
                      connected = TRUE;
                  else if (!connected && GetLastError() == ERROR_IO_PENDING)
                  {
                      while (!shared_stop.stop_requested())
                      {
                          const DWORD wait
                              = WaitForSingleObject(connect.hEvent, 100);
                          if (wait == WAIT_OBJECT_0)
                          {
                              DWORD ignored = 0;
                              connected = GetOverlappedResult(
                                  pipe, &connect, &ignored, FALSE);
                              break;
                          }
                          if (wait != WAIT_TIMEOUT)
                              break;
                      }
                  }
                  CloseHandle(connect.hEvent);
                  if (connected && !shared_stop.stop_requested())
                  {
                      std::string bytes;
                      ControlRequest request;
                      ControlMethodResult result;
                      if (!read_frame(pipe, bytes))
                      {
                          result = ControlMethodResult::error(
                              "invalid_frame", "Invalid control frame.");
                      }
                      else
                      {
                          result = parse_request(bytes, token, request);
                          if (result.ok)
                              result = dispatch(request);
                      }
                      const std::string response
                          = dump_wire_json(
                              response_json(request.id, result));
                      write_frame(pipe, response);
                      FlushFileBuffers(pipe);
                      DisconnectNamedPipe(pipe);
                  }
                  CancelIoEx(pipe, nullptr);
                  CloseHandle(pipe);
                  pipe = INVALID_HANDLE_VALUE;
              }
              if (pipe != INVALID_HANDLE_VALUE)
              {
                  CancelIoEx(pipe, nullptr);
                  CloseHandle(pipe);
              }
          };

    std::vector<std::jthread> additional_listeners;
    additional_listeners.reserve(3);
    for (int i = 0; i < 3; ++i)
    {
        additional_listeners.emplace_back(
            [serve_connections, stop_token](std::stop_token) {
                serve_connections(stop_token, INVALID_HANDLE_VALUE);
            });
    }
    serve_connections(stop_token, initial_pipe);
    // The listener closures reference the shared security descriptor. Join
    // them before releasing it.
    additional_listeners.clear();
    // Safety net: a stop requested before the first create would otherwise
    // leave start() waiting on a report that never comes.
    report_startup({});
    LocalFree(descriptor);
    active = false;
}

#else

void ControlServer::Impl::run(std::stop_token stop_token)
{
    const int server = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0)
    {
        report_startup("Unable to create the control socket.");
        active = false;
        return;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, endpoint.c_str(), endpoint.size() + 1);

    // Reclaim the path only when nothing is listening on it. Unlinking
    // unconditionally let a second instance silently steal a LIVE server's
    // endpoint: the first process kept polling a socket that no client could
    // reach, and every CLI request routed to the newcomer instead.
    if (::access(endpoint.c_str(), F_OK) == 0)
    {
        bool in_use = false;
        const int probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (probe >= 0)
        {
            in_use = ::connect(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
            ::close(probe);
        }
        if (in_use)
        {
            ::close(server);
            endpoint_in_use = true;
            report_startup("Control endpoint is already in use by another Draxul instance.");
            active = false;
            return;
        }
        ::unlink(endpoint.c_str()); // stale: the owner is gone
    }

    if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        const int bind_error = errno;
        ::close(server);
        const bool taken = bind_error == EADDRINUSE;
        endpoint_in_use = taken;
        report_startup(taken
                ? "Control endpoint is already in use by another Draxul instance."
                : "Unable to bind the control socket.");
        active = false;
        return;
    }
    if (::chmod(endpoint.c_str(), 0600) != 0
        || ::listen(server, 4) != 0)
    {
        ::close(server);
        ::unlink(endpoint.c_str());
        report_startup("Unable to prepare the control socket.");
        active = false;
        return;
    }
    report_startup({});
    const int flags = ::fcntl(server, F_GETFL, 0);
    ::fcntl(server, F_SETFL, flags | O_NONBLOCK);

    // Match the Windows listener pool. A client can occupy a connection for
    // the full I/O timeout, so one serial accept/read loop lets a stalled UI
    // starve every observer of the shared server.
    auto serve_connections = [this, server](std::stop_token shared_stop) {
        while (!shared_stop.stop_requested())
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
            ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                &timeout, sizeof(timeout));
            ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                &timeout, sizeof(timeout));

            std::string bytes;
            ControlRequest request;
            ControlMethodResult result;
            if (!read_frame(client, bytes))
            {
                result = ControlMethodResult::error(
                    "invalid_frame", "Invalid control frame.");
            }
            else
            {
                result = parse_request(bytes, token, request);
                if (result.ok)
                    result = dispatch(request);
            }
            const std::string response
                = dump_wire_json(response_json(request.id, result));
            write_frame(client, response);
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
    std::string* error,
    nlohmann::json metadata_extra)
{
    return impl_->start(std::move(session_id), std::move(runtime_directory),
        std::move(wake_main_thread), error, std::move(metadata_extra));
}

std::string namespaced_control_id(std::string_view base_id,
    const std::filesystem::path& runtime_directory)
{
    std::ostringstream id;
    id << base_id << '-' << std::hex
       << fnv1a(normalized_runtime_key(runtime_directory));
    return id.str();
}

void ControlServer::stop()
{
    impl_->stop();
}

bool ControlServer::running() const
{
    return impl_->active;
}

bool ControlServer::endpoint_in_use() const
{
    return impl_->endpoint_in_use;
}

uint32_t ControlServer::take_listener_error()
{
    return impl_->listener_error.exchange(0);
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
    nlohmann::json params,
    ControlRequestOptions options)
{
    const auto started_at = std::chrono::steady_clock::now();
    const auto timeout = std::max(
        std::chrono::milliseconds(1), options.timeout);
    const auto deadline = started_at + timeout;
    const auto metadata_path
        = control_metadata_path(runtime_directory, session_id);
    if (options.refresh_metadata)
        invalidate_cached_metadata(metadata_path);
    std::string endpoint;
    std::string token;
    std::string metadata_error;
    if (!read_cached_metadata(metadata_path,
            endpoint, token, metadata_error))
    {
        return { false, nullptr, "endpoint_unavailable", std::move(metadata_error) };
    }

    const auto wire_timeout = remaining_time(deadline);
    if (wire_timeout.count() == 0)
    {
        return { false, nullptr, "deadline_exceeded",
            "The Draxul control request exceeded its deadline." };
    }
    const std::string id = random_token().substr(0, 16);
    const std::string request_bytes = nlohmann::json{
        { "version", kControlProtocolVersion },
        { "token", token },
        { "id", id },
        { "method", method },
        { "params", params },
        { "timeout_ms", wire_timeout.count() },
    }
                                    .dump();

    std::string response_bytes;
    bool deadline_hit = false;
#ifdef _WIN32
    const std::wstring pipe_name(endpoint.begin(), endpoint.end());
    // The server services one connection per named-pipe instance and
    // immediately creates the next. A concurrent client can arrive in the
    // small close/recreate gap and see ERROR_FILE_NOT_FOUND even though the
    // server is healthy. Retry only that gap briefly; WaitNamedPipe retains
    // the longer I/O timeout for a live but occupied instance.
    const auto recreate_deadline
        = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    HANDLE pipe = INVALID_HANDLE_VALUE;
    do
    {
        const auto remaining = remaining_time(deadline);
        if (remaining.count() == 0)
            break;
        if (WaitNamedPipeW(pipe_name.c_str(),
                static_cast<DWORD>(remaining.count())))
        {
            pipe = CreateFileW(pipe_name.c_str(),
                GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED, nullptr);
            if (pipe != INVALID_HANDLE_VALUE)
                break;
        }
        const DWORD connect_error = GetLastError();
        if (connect_error != ERROR_FILE_NOT_FOUND
            && connect_error != ERROR_PIPE_BUSY)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now()
        < std::min(recreate_deadline, deadline));
    if (pipe == INVALID_HANDLE_VALUE)
    {
        invalidate_cached_metadata(metadata_path);
        if (!options.refresh_metadata)
        {
            return request(session_id, runtime_directory, method,
                std::move(params),
                {
                    .timeout = remaining_time(deadline),
                    .refresh_metadata = true,
                });
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return { false, nullptr, "deadline_exceeded",
                "The Draxul control request exceeded its deadline." };
        }
        return { false, nullptr, "endpoint_unavailable",
            "The Draxul Session control pipe is unavailable." };
    }
    const bool io_ok = client_write_frame(
                           pipe, request_bytes, deadline)
        && client_read_frame(pipe, response_bytes, deadline);
    deadline_hit = std::chrono::steady_clock::now() >= deadline
        || GetLastError() == ERROR_TIMEOUT;
    CloseHandle(pipe);
#else
    const int socket_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0)
        return { false, nullptr, "endpoint_unavailable", "Unable to create control socket." };
    const int socket_flags = ::fcntl(socket_fd, F_GETFL, 0);
    if (socket_flags < 0
        || ::fcntl(socket_fd, F_SETFL,
               socket_flags | O_NONBLOCK)
            != 0)
    {
        ::close(socket_fd);
        return { false, nullptr, "endpoint_unavailable",
            "Unable to configure the control socket." };
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (endpoint.size() >= sizeof(address.sun_path))
    {
        ::close(socket_fd);
        return { false, nullptr, "endpoint_unavailable", "Control socket path is too long." };
    }
    std::memcpy(address.sun_path, endpoint.c_str(), endpoint.size() + 1);
    bool connected = ::connect(socket_fd,
                         reinterpret_cast<sockaddr*>(&address),
                         sizeof(address))
        == 0;
    if (!connected && errno == EINPROGRESS
        && wait_for_socket(socket_fd, POLLOUT, deadline))
    {
        int socket_error = 0;
        socklen_t socket_error_size = sizeof(socket_error);
        connected = ::getsockopt(socket_fd, SOL_SOCKET, SO_ERROR,
                        &socket_error, &socket_error_size)
                == 0
            && socket_error == 0;
        if (!connected && socket_error != 0)
            errno = socket_error;
    }
    if (!connected && !options.refresh_metadata)
    {
        ::close(socket_fd);
        invalidate_cached_metadata(metadata_path);
        return request(session_id, runtime_directory, method,
            std::move(params),
            {
                .timeout = remaining_time(deadline),
                .refresh_metadata = true,
            });
    }
    const bool io_ok = connected
        && client_write_frame(
            socket_fd, request_bytes, deadline)
        && client_read_frame(socket_fd, response_bytes, deadline);
    deadline_hit = std::chrono::steady_clock::now() >= deadline
        || errno == ETIMEDOUT;
    ::close(socket_fd);
#endif
    if (!io_ok)
    {
        invalidate_cached_metadata(metadata_path);
        if (deadline_hit)
        {
            return { false, nullptr, "deadline_exceeded",
                "The Draxul control request exceeded its deadline." };
        }
        return { false, nullptr, "io_error", "Control request failed." };
    }
    if (!depth_within_limit(response_bytes))
        return { false, nullptr, "invalid_response",
            "Control response exceeds the JSON nesting limit." };

    const auto response = nlohmann::json::parse(response_bytes, nullptr, false, true);
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
    ControlClientResult result{
        false,
        nullptr,
        response["error"].value("code", "unknown_error"),
        response["error"].value("message", "Control request failed."),
    };
    if (result.error_code == "authentication_failed"
        && !options.refresh_metadata)
    {
        invalidate_cached_metadata(metadata_path);
        return request(session_id, runtime_directory, method,
            std::move(params),
            {
                .timeout = remaining_time(deadline),
                .refresh_metadata = true,
            });
    }
    return result;
}

} // namespace draxul
