#include <draxul/async_frame_stream.h>

#include "control_codec.h"

#include <draxul/control_plane.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <sddl.h>
// clang-format on

namespace draxul
{
namespace
{

void set_error(AsyncFrameStreamError& error, std::string code,
    std::string message, DWORD native_code = ERROR_SUCCESS)
{
    error = { std::move(code), std::move(message), native_code };
}

bool create_current_user_security_descriptor(
    PSECURITY_DESCRIPTOR& descriptor, AsyncFrameStreamError& error)
{
    descriptor = nullptr;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        set_error(error, "io_error",
            "Unable to inspect the current Windows user.", GetLastError());
        return false;
    }
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<BYTE> buffer(bytes);
    if (bytes == 0 || !GetTokenInformation(
            token, TokenUser, buffer.data(), bytes, &bytes))
    {
        const DWORD code = GetLastError();
        CloseHandle(token);
        set_error(error, "io_error",
            "Unable to read the current Windows user.", code);
        return false;
    }
    CloseHandle(token);
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &sid))
    {
        set_error(error, "io_error",
            "Unable to encode the current Windows user.", GetLastError());
        return false;
    }
    const std::wstring sddl
        = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
    {
        set_error(error, "io_error",
            "Unable to secure the Session stream endpoint.", GetLastError());
        return false;
    }
    return true;
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
    return std::string(R"(\\.\pipe\draxul-session-stream-)") + suffix;
}

} // namespace

class AsyncFrameStreamConnection::Impl
{
public:
    explicit Impl(HANDLE value)
        : handle(value)
    {
    }

    ~Impl()
    {
        close();
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }

    void close()
    {
        if (!closed.exchange(true) && handle != INVALID_HANDLE_VALUE)
            CancelIoEx(handle, nullptr);
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
            OVERLAPPED operation{};
            operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!operation.hEvent)
            {
                set_error(error, "io_error",
                    "Unable to create a Session stream I/O event.", GetLastError());
                return false;
            }
            DWORD transferred = 0;
            const DWORD remaining = static_cast<DWORD>(
                std::min<size_t>(size - offset, MAXDWORD));
            BOOL ok = reading
                ? ReadFile(handle, static_cast<char*>(data) + offset,
                      remaining, &transferred, &operation)
                : WriteFile(handle, static_cast<const char*>(data) + offset,
                      remaining, &transferred, &operation);
            DWORD code = ok ? ERROR_SUCCESS : GetLastError();
            if (!ok && code == ERROR_IO_PENDING)
            {
                for (;;)
                {
                    const DWORD wait
                        = WaitForSingleObject(operation.hEvent, 50);
                    if (wait == WAIT_OBJECT_0)
                    {
                        ok = GetOverlappedResult(handle, &operation,
                            &transferred, FALSE);
                        code = ok ? ERROR_SUCCESS : GetLastError();
                        break;
                    }
                    if (wait != WAIT_TIMEOUT || closed
                        || stop_token.stop_requested()
                        || (!reading
                            && std::chrono::steady_clock::now()
                                >= write_deadline))
                    {
                        CancelIoEx(handle, &operation);
                        WaitForSingleObject(operation.hEvent, INFINITE);
                        DWORD ignored = 0;
                        GetOverlappedResult(handle, &operation,
                            &ignored, FALSE);
                        code = !reading
                                && std::chrono::steady_clock::now()
                                    >= write_deadline
                            ? ERROR_TIMEOUT
                            : ERROR_OPERATION_ABORTED;
                        ok = FALSE;
                        break;
                    }
                }
            }
            CloseHandle(operation.hEvent);
            if (!ok || transferred == 0)
            {
                if (code == ERROR_TIMEOUT)
                {
                    set_error(error, "deadline_exceeded",
                        "Session stream write exceeded its deadline.", code);
                }
                else if (code == ERROR_OPERATION_ABORTED
                    || closed || stop_token.stop_requested())
                {
                    set_error(error, "cancelled",
                        "Session stream I/O was cancelled.", code);
                }
                else if (code == ERROR_BROKEN_PIPE
                    || code == ERROR_PIPE_NOT_CONNECTED
                    || transferred == 0)
                {
                    set_error(error, "closed",
                        "The Session stream peer closed the connection.", code);
                }
                else
                {
                    set_error(error, "io_error",
                        "Session stream I/O failed.", code);
                }
                return false;
            }
            offset += transferred;
        }
        error = {};
        return true;
    }

    HANDLE handle = INVALID_HANDLE_VALUE;
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
    const std::wstring name(endpoint.begin(), endpoint.end());
    const auto deadline = std::chrono::steady_clock::now()
        + std::max(timeout, std::chrono::milliseconds(1));
    HANDLE pipe = INVALID_HANDLE_VALUE;
    do
    {
        pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE)
            break;
        const DWORD code = GetLastError();
        if (code != ERROR_PIPE_BUSY && code != ERROR_FILE_NOT_FOUND)
        {
            set_error(error, "io_error",
                "Unable to connect to the Session stream.", code);
            return nullptr;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        set_error(error, "deadline_exceeded",
            "Timed out connecting to the Session stream.", GetLastError());
        return nullptr;
    }
    error = {};
    return std::unique_ptr<AsyncFrameStreamConnection>(
        new AsyncFrameStreamConnection(
            std::make_unique<AsyncFrameStreamConnection::Impl>(pipe)));
}

class AsyncFrameStreamListener::Impl
{
public:
    ~Impl() { stop(); }

    void stop()
    {
        active = false;
        std::lock_guard guard(mutex);
        if (pending != INVALID_HANDLE_VALUE)
            CancelIoEx(pending, nullptr);
    }

    std::string endpoint_value;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    std::atomic<bool> active = false;
    mutable std::mutex mutex;
    HANDLE pending = INVALID_HANDLE_VALUE;
};

AsyncFrameStreamListener::AsyncFrameStreamListener()
    : impl_(std::make_unique<Impl>())
{
}

AsyncFrameStreamListener::~AsyncFrameStreamListener()
{
    stop();
    if (impl_->descriptor)
        LocalFree(impl_->descriptor);
}

bool AsyncFrameStreamListener::start(std::string_view stream_id,
    const std::filesystem::path& runtime_directory,
    AsyncFrameStreamError& error)
{
    stop();
    if (impl_->descriptor)
    {
        LocalFree(impl_->descriptor);
        impl_->descriptor = nullptr;
    }
    if (!create_current_user_security_descriptor(
            impl_->descriptor, error))
        return false;
    impl_->endpoint_value = stream_endpoint(stream_id, runtime_directory);
    impl_->active = true;
    error = {};
    return true;
}

std::unique_ptr<AsyncFrameStreamConnection> AsyncFrameStreamListener::accept(
    std::stop_token stop_token, AsyncFrameStreamError& error)
{
    if (!impl_->active)
    {
        set_error(error, "cancelled", "Session stream listener is stopped.");
        return nullptr;
    }
    SECURITY_ATTRIBUTES attributes{
        sizeof(SECURITY_ATTRIBUTES), impl_->descriptor, FALSE
    };
    const std::wstring name(
        impl_->endpoint_value.begin(), impl_->endpoint_value.end());
    HANDLE pipe = CreateNamedPipeW(name.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT
            | PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, &attributes);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        set_error(error, "io_error",
            "Unable to create the Session stream listener.", GetLastError());
        return nullptr;
    }
    {
        std::lock_guard guard(impl_->mutex);
        impl_->pending = pipe;
    }
    OVERLAPPED operation{};
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL connected = operation.hEvent
        ? ConnectNamedPipe(pipe, &operation)
        : FALSE;
    DWORD code = connected ? ERROR_SUCCESS : GetLastError();
    if (!connected && code == ERROR_PIPE_CONNECTED)
        connected = TRUE;
    else if (!connected && code == ERROR_IO_PENDING)
    {
        while (impl_->active && !stop_token.stop_requested())
        {
            const DWORD wait = WaitForSingleObject(operation.hEvent, 50);
            if (wait == WAIT_OBJECT_0)
            {
                DWORD ignored = 0;
                connected = GetOverlappedResult(
                    pipe, &operation, &ignored, FALSE);
                code = connected ? ERROR_SUCCESS : GetLastError();
                break;
            }
            if (wait != WAIT_TIMEOUT)
            {
                code = GetLastError();
                break;
            }
        }
    }
    if (!connected)
    {
        CancelIoEx(pipe, &operation);
        if (operation.hEvent)
            WaitForSingleObject(operation.hEvent, INFINITE);
    }
    if (operation.hEvent)
        CloseHandle(operation.hEvent);
    {
        std::lock_guard guard(impl_->mutex);
        impl_->pending = INVALID_HANDLE_VALUE;
    }
    if (!connected || !impl_->active || stop_token.stop_requested())
    {
        CloseHandle(pipe);
        set_error(error,
            (!impl_->active || stop_token.stop_requested())
                ? "cancelled" : "io_error",
            (!impl_->active || stop_token.stop_requested())
                ? "Session stream accept was cancelled."
                : "Unable to accept the Session stream connection.",
            code);
        return nullptr;
    }
    error = {};
    return std::unique_ptr<AsyncFrameStreamConnection>(
        new AsyncFrameStreamConnection(
            std::make_unique<AsyncFrameStreamConnection::Impl>(pipe)));
}

const std::string& AsyncFrameStreamListener::endpoint() const
{
    return impl_->endpoint_value;
}

void AsyncFrameStreamListener::stop()
{
    if (impl_)
        impl_->stop();
}

bool AsyncFrameStreamListener::running() const
{
    return impl_ && impl_->active;
}

} // namespace draxul
