#include "control_transport.h"

#include "control_codec.h"
#include "control_exact_io.h"

#include <draxul/control_plane.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// sddl.h depends on Windows API family declarations from windows.h.
// clang-format off
#include <windows.h>
#include <sddl.h>
// clang-format on

namespace draxul::control_detail
{
namespace
{

constexpr auto kIoTimeout = std::chrono::seconds(5);

TransportError win32_error(TransportStage stage, DWORD code,
    FailureClass classification, std::string message)
{
    return {
        .stage = stage,
        .domain = NativeDomain::Win32,
        .native_code = code,
        .classification = classification,
        .message = std::move(message),
    };
}

bool create_current_user_security_descriptor(
    PSECURITY_DESCRIPTOR& descriptor, std::string& error)
{
    descriptor = nullptr;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        error = "Unable to open the current Windows user token.";
        return false;
    }

    DWORD token_bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &token_bytes);
    if (token_bytes == 0)
    {
        CloseHandle(token);
        error = "Unable to size the current Windows user identity.";
        return false;
    }
    std::vector<BYTE> token_buffer(token_bytes);
    if (!GetTokenInformation(token, TokenUser, token_buffer.data(),
            token_bytes, &token_bytes))
    {
        CloseHandle(token);
        error = "Unable to read the current Windows user identity.";
        return false;
    }
    CloseHandle(token);

    const auto* token_user
        = reinterpret_cast<const TOKEN_USER*>(token_buffer.data());
    LPWSTR sid_text = nullptr;
    if (!ConvertSidToStringSidW(token_user->User.Sid, &sid_text))
    {
        error = "Unable to encode the current Windows user identity.";
        return false;
    }
    const std::wstring sddl
        = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid_text) + L")";
    LocalFree(sid_text);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
    {
        error = "Unable to create the current-user security descriptor.";
        return false;
    }
    return true;
}

bool create_current_user_directory_security_descriptor(
    PSECURITY_DESCRIPTOR& descriptor, std::string& error)
{
    descriptor = nullptr;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        error = "Unable to open the current Windows user token.";
        return false;
    }
    DWORD token_bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &token_bytes);
    if (token_bytes == 0)
    {
        CloseHandle(token);
        error = "Unable to size the current Windows user identity.";
        return false;
    }
    std::vector<BYTE> token_buffer(token_bytes);
    if (!GetTokenInformation(token, TokenUser, token_buffer.data(),
            token_bytes, &token_bytes))
    {
        CloseHandle(token);
        error = "Unable to read the current Windows user identity.";
        return false;
    }
    CloseHandle(token);
    const auto* token_user
        = reinterpret_cast<const TOKEN_USER*>(token_buffer.data());
    LPWSTR sid_text = nullptr;
    if (!ConvertSidToStringSidW(token_user->User.Sid, &sid_text))
    {
        error = "Unable to encode the current Windows user identity.";
        return false;
    }
    const std::wstring sddl
        = L"D:P(A;OICI;GA;;;SY)(A;OICI;GA;;;" + std::wstring(sid_text) + L")";
    LocalFree(sid_text);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
    {
        error = "Unable to create the current-user security descriptor.";
        return false;
    }
    return true;
}

bool await_server_io(HANDLE handle, OVERLAPPED& overlapped,
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
        return GetOverlappedResult(handle, &overlapped, &transferred, FALSE);

    const DWORD error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
    CancelIoEx(handle, &overlapped);
    WaitForSingleObject(overlapped.hEvent, INFINITE);
    DWORD ignored = 0;
    GetOverlappedResult(handle, &overlapped, &ignored, FALSE);
    SetLastError(error);
    return false;
}

TransportStatus server_read_exact(
    HANDLE handle, void* data, size_t size, TransportStage stage)
{
    return read_exact(
        [handle](void* destination, size_t remaining,
            TransportStage current_stage) {
            OVERLAPPED overlapped{};
            overlapped.hEvent
                = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!overlapped.hEvent)
            {
                return IoAttemptResult::failure(win32_error(
                    current_stage, GetLastError(), FailureClass::IoError,
                    "Control request failed."));
            }
            DWORD read = 0;
            BOOL ok = ReadFile(handle, destination,
                static_cast<DWORD>(remaining), &read, &overlapped);
            if (!ok)
            {
                ok = await_server_io(
                    handle, overlapped, read, GetLastError());
            }
            const DWORD io_error = ok ? ERROR_SUCCESS : GetLastError();
            CloseHandle(overlapped.hEvent);
            if (!ok)
            {
                return IoAttemptResult::failure(win32_error(
                    current_stage, io_error, FailureClass::IoError,
                    "Control request failed."));
            }
            if (read == 0)
            {
                return IoAttemptResult::end_of_stream(win32_error(
                    current_stage, ERROR_BROKEN_PIPE,
                    FailureClass::IoError, "Control request failed."));
            }
            return IoAttemptResult::progress(read);
        },
        data, size, stage);
}

TransportStatus server_write_exact(
    HANDLE handle, const void* data, size_t size, TransportStage stage)
{
    return write_exact(
        [handle](const void* source, size_t remaining,
            TransportStage current_stage) {
            OVERLAPPED overlapped{};
            overlapped.hEvent
                = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!overlapped.hEvent)
            {
                return IoAttemptResult::failure(win32_error(
                    current_stage, GetLastError(), FailureClass::IoError,
                    "Control request failed."));
            }
            DWORD written = 0;
            BOOL ok = WriteFile(handle, source,
                static_cast<DWORD>(remaining), &written, &overlapped);
            if (!ok)
            {
                ok = await_server_io(
                    handle, overlapped, written, GetLastError());
            }
            const DWORD io_error = ok ? ERROR_SUCCESS : GetLastError();
            CloseHandle(overlapped.hEvent);
            if (!ok)
            {
                return IoAttemptResult::failure(win32_error(
                    current_stage, io_error, FailureClass::IoError,
                    "Control request failed."));
            }
            if (written == 0)
            {
                return IoAttemptResult::end_of_stream(win32_error(
                    current_stage, ERROR_BROKEN_PIPE,
                    FailureClass::IoError, "Control request failed."));
            }
            return IoAttemptResult::progress(written);
        },
        data, size, stage);
}

bool await_client_io(HANDLE handle, OVERLAPPED& overlapped,
    DWORD& transferred, DWORD initial_error, ControlDeadline deadline,
    TransportStage stage, TransportError& error)
{
    if (initial_error != ERROR_IO_PENDING)
    {
        error = win32_error(stage, initial_error, FailureClass::IoError,
            "Control request failed.");
        return false;
    }
    const auto remaining = remaining_time(deadline);
    const DWORD wait = WaitForSingleObject(
        overlapped.hEvent, static_cast<DWORD>(remaining.count()));
    if (wait == WAIT_OBJECT_0)
    {
        if (GetOverlappedResult(handle, &overlapped, &transferred, FALSE))
            return true;
        error = win32_error(stage, GetLastError(), FailureClass::IoError,
            "Control request failed.");
        return false;
    }

    const DWORD code = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
    CancelIoEx(handle, &overlapped);
    WaitForSingleObject(overlapped.hEvent, INFINITE);
    DWORD ignored = 0;
    GetOverlappedResult(handle, &overlapped, &ignored, FALSE);
    error = win32_error(stage, code,
        code == ERROR_TIMEOUT ? FailureClass::DeadlineExceeded
                              : FailureClass::IoError,
        code == ERROR_TIMEOUT
            ? "The Draxul control request exceeded its deadline."
            : "Control request failed.");
    return false;
}

bool client_read_exact(HANDLE handle, void* data, size_t size,
    ControlDeadline deadline, TransportStage stage, TransportError& error)
{
    auto status = read_exact(
        [handle, deadline](void* destination, size_t remaining,
            TransportStage current_stage) {
            if (remaining_time(deadline).count() == 0)
            {
                return IoAttemptResult::failure(win32_error(
                    current_stage, ERROR_TIMEOUT,
                    FailureClass::DeadlineExceeded,
                    "The Draxul control request exceeded its deadline."));
            }
            OVERLAPPED overlapped{};
            overlapped.hEvent
                = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!overlapped.hEvent)
            {
                return IoAttemptResult::failure(win32_error(
                    current_stage, GetLastError(), FailureClass::IoError,
                    "Control request failed."));
            }
            DWORD read = 0;
            TransportError attempt_error;
            BOOL ok = ReadFile(handle, destination,
                static_cast<DWORD>(remaining), &read, &overlapped);
            if (!ok)
            {
                ok = await_client_io(handle, overlapped, read,
                    GetLastError(), deadline, current_stage,
                    attempt_error);
            }
            CloseHandle(overlapped.hEvent);
            if (!ok)
                return IoAttemptResult::failure(std::move(attempt_error));
            if (read == 0)
            {
                return IoAttemptResult::end_of_stream(win32_error(
                    current_stage, ERROR_BROKEN_PIPE,
                    FailureClass::IoError, "Control request failed."));
            }
            return IoAttemptResult::progress(read);
        },
        data, size, stage);
    if (!status.ok)
        error = status.error;
    return status.ok;
}

bool client_write_exact(HANDLE handle, const void* data, size_t size,
    ControlDeadline deadline, TransportStage stage, TransportError& error)
{
    auto status = write_exact(
        [handle, deadline](const void* source, size_t remaining,
            TransportStage current_stage) {
            if (remaining_time(deadline).count() == 0)
            {
                return IoAttemptResult::failure(win32_error(
                    current_stage, ERROR_TIMEOUT,
                    FailureClass::DeadlineExceeded,
                    "The Draxul control request exceeded its deadline."));
            }
            OVERLAPPED overlapped{};
            overlapped.hEvent
                = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!overlapped.hEvent)
            {
                return IoAttemptResult::failure(win32_error(
                    current_stage, GetLastError(), FailureClass::IoError,
                    "Control request failed."));
            }
            DWORD written = 0;
            TransportError attempt_error;
            BOOL ok = WriteFile(handle, source,
                static_cast<DWORD>(remaining), &written, &overlapped);
            if (!ok)
            {
                ok = await_client_io(handle, overlapped, written,
                    GetLastError(), deadline, current_stage,
                    attempt_error);
            }
            CloseHandle(overlapped.hEvent);
            if (!ok)
                return IoAttemptResult::failure(std::move(attempt_error));
            if (written == 0)
            {
                return IoAttemptResult::end_of_stream(win32_error(
                    current_stage, ERROR_BROKEN_PIPE,
                    FailureClass::IoError, "Control request failed."));
            }
            return IoAttemptResult::progress(written);
        },
        data, size, stage);
    if (!status.ok)
        error = status.error;
    return status.ok;
}

class Win32ServerTransport final : public ServerTransport
{
public:
    explicit Win32ServerTransport(
        const ListenerCreateTestHooks* test_hooks)
        : test_hooks_(test_hooks)
    {
    }

    TransportStatus prepare(std::string_view session_id,
        const std::filesystem::path&) override
    {
        endpoint_ = R"(\\.\pipe\draxul-control-)" + session_key(session_id);
        endpoint_in_use_ = false;
        listener_error_ = 0;
        abandoned_ = false;
        return TransportStatus::success();
    }

    void run(std::stop_token stop_token,
        const ServerFrameHandler& handle_frame,
        const StartupReporter& report_startup,
        const ServerTransportObserver& observer) override
    {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        std::string descriptor_error;
        if (!create_current_user_security_descriptor(
                descriptor, descriptor_error))
        {
            report_startup(
                "Unable to build the control pipe security descriptor: "
                + descriptor_error);
            return;
        }
        SECURITY_ATTRIBUTES attributes{
            sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE
        };
        const std::wstring pipe_name(endpoint_.begin(), endpoint_.end());
        HANDLE initial_pipe = CreateNamedPipeW(pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED
                | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT
                | PIPE_REJECT_REMOTE_CLIENTS,
            4, static_cast<DWORD>(kControlMaxMessageBytes),
            static_cast<DWORD>(kControlMaxMessageBytes), 0, &attributes);
        if (initial_pipe == INVALID_HANDLE_VALUE)
        {
            const DWORD create_error = GetLastError();
            const bool taken = create_error == ERROR_ACCESS_DENIED
                || create_error == ERROR_PIPE_BUSY;
            endpoint_in_use_ = taken;
            report_startup(taken
                    ? "Control endpoint is already in use by another Draxul instance."
                    : "Unable to create the control pipe.");
            LocalFree(descriptor);
            return;
        }
        report_startup({});

        auto serve_connections
            = [this, &attributes, &pipe_name, &handle_frame, &observer](
                  std::stop_token shared_stop, HANDLE first_pipe,
                  bool keep_first_instance) {
                  HANDLE pipe = first_pipe;
                  while (!shared_stop.stop_requested())
                  {
                      if (pipe == INVALID_HANDLE_VALUE)
                      {
                          std::optional<uint32_t> injected_error;
                          if (test_hooks_
                              && test_hooks_->fail_recreation)
                          {
                              injected_error
                                  = test_hooks_->fail_recreation();
                          }
                          if (injected_error)
                          {
                              SetLastError(
                                  static_cast<DWORD>(*injected_error));
                          }
                          else
                          {
                              pipe = CreateNamedPipeW(pipe_name.c_str(),
                                  PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                  PIPE_TYPE_BYTE | PIPE_READMODE_BYTE
                                      | PIPE_WAIT
                                      | PIPE_REJECT_REMOTE_CLIENTS,
                                  4,
                                  static_cast<DWORD>(
                                      kControlMaxMessageBytes),
                                  static_cast<DWORD>(
                                      kControlMaxMessageBytes),
                                  0, &attributes);
                          }
                          if (pipe == INVALID_HANDLE_VALUE)
                          {
                              listener_error_ = GetLastError();
                              std::this_thread::sleep_for(
                                  std::chrono::milliseconds(25));
                              continue;
                          }
                          if (test_hooks_
                              && test_hooks_->recreation_succeeded)
                          {
                              test_hooks_->recreation_succeeded();
                          }
                      }

                      OVERLAPPED connect{};
                      connect.hEvent
                          = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                      if (!connect.hEvent)
                      {
                          listener_error_ = GetLastError();
                          if (!keep_first_instance)
                          {
                              CloseHandle(pipe);
                              pipe = INVALID_HANDLE_VALUE;
                          }
                          std::this_thread::sleep_for(
                              std::chrono::milliseconds(25));
                          continue;
                      }
                      BOOL connected = ConnectNamedPipe(pipe, &connect);
                      DWORD connect_error = connected
                          ? ERROR_SUCCESS
                          : GetLastError();
                      if (!connected && connect_error == ERROR_PIPE_CONNECTED)
                          connected = TRUE;
                      else if (!connected
                          && connect_error == ERROR_IO_PENDING)
                      {
                          bool pending = true;
                          while (!shared_stop.stop_requested())
                          {
                              const DWORD wait
                                  = WaitForSingleObject(connect.hEvent, 100);
                              if (wait == WAIT_OBJECT_0)
                              {
                                  DWORD ignored = 0;
                                  connected = GetOverlappedResult(
                                      pipe, &connect, &ignored, FALSE);
                                  pending = false;
                                  break;
                              }
                              if (wait != WAIT_TIMEOUT)
                                  break;
                          }
                          if (pending)
                          {
                              // OVERLAPPED and its event must outlive the
                              // pending connect. Cancel and drain it before
                              // either stack object is released.
                              CancelIoEx(pipe, &connect);
                              WaitForSingleObject(connect.hEvent, INFINITE);
                              DWORD ignored = 0;
                              GetOverlappedResult(
                                  pipe, &connect, &ignored, FALSE);
                          }
                      }
                      CloseHandle(connect.hEvent);
                      if (connected && !shared_stop.stop_requested())
                      {
                          if (observer.connection_opened)
                              observer.connection_opened();
                          std::string bytes;
                          const auto read_status = read_control_frame(
                              [pipe](void* data, size_t size,
                                  TransportStage stage) {
                                  return server_read_exact(
                                      pipe, data, size, stage);
                              },
                              bytes);
                          if (!read_status.ok && observer.transport_failed)
                              observer.transport_failed(read_status.error);
                          const std::optional<std::string> request
                              = read_status.ok
                              ? std::optional<std::string>(std::move(bytes))
                              : std::nullopt;
                          const auto response = handle_frame(request);
                          const auto write_status = write_control_frame(
                              [pipe](const void* data, size_t size,
                                  TransportStage stage) {
                                  return server_write_exact(
                                      pipe, data, size, stage);
                              },
                              response.bytes);
                          if (!write_status.ok && observer.transport_failed)
                              observer.transport_failed(write_status.error);
                          FlushFileBuffers(pipe);
                          if (!response.method.empty()
                              && observer.response_completed)
                          {
                              observer.response_completed(response.method,
                                  response.failed,
                                  static_cast<uint64_t>(std::max<int64_t>(0,
                                      std::chrono::duration_cast<
                                          std::chrono::microseconds>(
                                          std::chrono::steady_clock::now()
                                          - response.started_at)
                                          .count())));
                          }
                          DisconnectNamedPipe(pipe);
                          if (observer.connection_closed)
                              observer.connection_closed();
                      }
                      CancelIoEx(pipe, nullptr);
                      if (!keep_first_instance)
                      {
                          CloseHandle(pipe);
                          pipe = INVALID_HANDLE_VALUE;
                      }
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
                    serve_connections(
                        stop_token, INVALID_HANDLE_VALUE, false);
                });
        }
        serve_connections(stop_token, initial_pipe, true);
        additional_listeners.clear();
        report_startup({});
        LocalFree(descriptor);
    }

    const std::string& endpoint() const override { return endpoint_; }
    bool endpoint_in_use() const override { return endpoint_in_use_; }
    void abandon_endpoint() override { abandoned_ = true; }
    void cleanup() override { }
    uint32_t take_listener_error() override
    {
        return listener_error_.exchange(0);
    }

private:
    std::string endpoint_;
    std::atomic<bool> endpoint_in_use_ = false;
    std::atomic<uint32_t> listener_error_ = 0;
    bool abandoned_ = false;
    const ListenerCreateTestHooks* test_hooks_ = nullptr;
};

} // namespace

TransportStatus secure_runtime_directory(
    const std::filesystem::path& runtime_directory)
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    std::string error;
    if (!create_current_user_directory_security_descriptor(descriptor, error))
    {
        return TransportStatus::failure(win32_error(
            TransportStage::RuntimeSecurity, GetLastError(),
            FailureClass::EndpointUnavailable, std::move(error)));
    }
    const bool secured = SetFileSecurityW(runtime_directory.c_str(),
                              DACL_SECURITY_INFORMATION
                                  | PROTECTED_DACL_SECURITY_INFORMATION,
                              descriptor)
        != FALSE;
    const DWORD code = secured ? ERROR_SUCCESS : GetLastError();
    LocalFree(descriptor);
    if (!secured)
    {
        return TransportStatus::failure(win32_error(
            TransportStage::RuntimeSecurity, code,
            FailureClass::EndpointUnavailable,
            "Unable to secure the control runtime path."));
    }
    return TransportStatus::success();
}

TransportStatus write_current_user_metadata(
    const std::filesystem::path& path, std::string_view contents)
{
    std::filesystem::path temporary = path;
    temporary += ".tmp-" + random_token();
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    std::string descriptor_error;
    if (!create_current_user_security_descriptor(
            descriptor, descriptor_error))
    {
        return TransportStatus::failure(win32_error(
            TransportStage::MetadataCreate, GetLastError(),
            FailureClass::EndpointUnavailable,
            "Unable to create control metadata security descriptor: "
                + descriptor_error));
    }
    SECURITY_ATTRIBUTES attributes{
        sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE
    };
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0,
        &attributes, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr);
    const DWORD create_error
        = file == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    LocalFree(descriptor);
    if (file == INVALID_HANDLE_VALUE)
    {
        return TransportStatus::failure(win32_error(
            TransportStage::MetadataCreate, create_error,
            FailureClass::EndpointUnavailable,
            "Unable to create temporary control metadata file."));
    }

    DWORD written = 0;
    const bool wrote = contents.size() <= MAXDWORD
        && WriteFile(file, contents.data(),
            static_cast<DWORD>(contents.size()), &written, nullptr)
        && written == contents.size();
    DWORD code = wrote ? ERROR_SUCCESS : GetLastError();
    bool flushed = false;
    if (wrote)
    {
        flushed = FlushFileBuffers(file) != FALSE;
        if (!flushed)
            code = GetLastError();
    }
    CloseHandle(file);
    if (!wrote || !flushed)
    {
        DeleteFileW(temporary.c_str());
        return TransportStatus::failure(win32_error(
            wrote ? TransportStage::MetadataFlush
                  : TransportStage::MetadataWrite,
            code, FailureClass::EndpointUnavailable,
            "Unable to write control metadata file."));
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        code = GetLastError();
        DeleteFileW(temporary.c_str());
        return TransportStatus::failure(win32_error(
            TransportStage::MetadataReplace, code,
            FailureClass::EndpointUnavailable,
            "Unable to atomically replace control metadata file."));
    }
    return TransportStatus::success();
}

ClientExchangeResult client_exchange(std::string_view endpoint,
    std::string_view request_bytes, ControlDeadline deadline)
{
    const std::wstring pipe_name(endpoint.begin(), endpoint.end());
    const auto recreate_deadline
        = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    HANDLE pipe = INVALID_HANDLE_VALUE;
    DWORD last_error = ERROR_FILE_NOT_FOUND;
    TransportStage connect_stage = TransportStage::ConnectWait;
    do
    {
        const auto remaining = remaining_time(deadline);
        if (remaining.count() == 0)
        {
            last_error = ERROR_TIMEOUT;
            break;
        }
        if (WaitNamedPipeW(
                pipe_name.c_str(), static_cast<DWORD>(remaining.count())))
        {
            pipe = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE,
                0, nullptr, OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT
                    | SECURITY_IDENTIFICATION,
                nullptr);
            connect_stage = TransportStage::Connect;
            if (pipe != INVALID_HANDLE_VALUE)
                break;
        }
        last_error = GetLastError();
        if (last_error != ERROR_FILE_NOT_FOUND
            && last_error != ERROR_PIPE_BUSY)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now()
        < std::min(recreate_deadline, deadline));

    if (pipe == INVALID_HANDLE_VALUE)
    {
        const bool timed_out = std::chrono::steady_clock::now() >= deadline
            || last_error == ERROR_TIMEOUT;
        return {
            .ok = false,
            .error = win32_error(connect_stage, last_error,
                timed_out ? FailureClass::DeadlineExceeded
                          : FailureClass::EndpointUnavailable,
                timed_out
                    ? "The Draxul control request exceeded its deadline."
                    : "The Draxul Session control pipe is unavailable."),
        };
    }

    TransportError error;
    auto write_status = write_control_frame(
        [pipe, deadline, &error](const void* data, size_t size,
            TransportStage stage) {
            if (client_write_exact(
                    pipe, data, size, deadline, stage, error))
                return TransportStatus::success();
            return TransportStatus::failure(error);
        },
        request_bytes);
    std::string response;
    TransportStatus read_status = TransportStatus::failure(write_status.error);
    if (write_status.ok)
        read_status = read_control_frame(
            [pipe, deadline, &error](void* data, size_t size,
                TransportStage stage) {
                if (client_read_exact(
                        pipe, data, size, deadline, stage, error))
                    return TransportStatus::success();
                return TransportStatus::failure(error);
            },
            response);
    CloseHandle(pipe);
    if (!write_status.ok)
        return { .ok = false, .error = std::move(write_status.error) };
    if (!read_status.ok)
        return { .ok = false, .error = std::move(read_status.error) };
    return { .ok = true, .response_bytes = std::move(response) };
}

std::unique_ptr<ServerTransport> make_server_transport(
    const ListenerCreateTestHooks* test_hooks)
{
    return std::make_unique<Win32ServerTransport>(test_hooks);
}

} // namespace draxul::control_detail
