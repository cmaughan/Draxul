#include "session_attach_internal.h"

#include <draxul/log.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <aclapi.h>
#include <sddl.h>
#include <windows.h>
#pragma comment(lib, "Advapi32.lib")

namespace draxul::session_attach_detail
{

namespace
{

std::string mutex_name(std::string_view session_id)
{
    return "Local\\DraxulSessionAttach-" + endpoint_suffix(session_id);
}

std::string pipe_name(std::string_view session_id)
{
    return "\\\\.\\pipe\\draxul-session-attach-" + endpoint_suffix(session_id);
}

std::string win32_error_message(DWORD error)
{
    LPSTR buffer = nullptr;
    const DWORD flags
        = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD size = FormatMessageA(
        flags, nullptr, error, 0, reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string message = size > 0 && buffer
        ? std::string(buffer, size)
        : ("Win32 error " + std::to_string(error));
    if (buffer)
        LocalFree(buffer);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
        message.pop_back();
    return message;
}

struct ScopedTokenHandle
{
    HANDLE handle = nullptr;
    ~ScopedTokenHandle()
    {
        if (handle)
            CloseHandle(handle);
    }
};

struct ScopedLocalAlloc
{
    void* ptr = nullptr;
    ~ScopedLocalAlloc()
    {
        if (ptr)
            LocalFree(ptr);
    }
};

struct ScopedSid
{
    PSID sid = nullptr;
    ~ScopedSid()
    {
        if (sid)
            FreeSid(sid);
    }
};

struct OwnedSid
{
    std::vector<BYTE> bytes;
    bool empty() const { return bytes.empty(); }
    PSID get() { return bytes.empty() ? nullptr : static_cast<PSID>(bytes.data()); }
    PSID get() const { return bytes.empty() ? nullptr : const_cast<BYTE*>(bytes.data()); }
};

struct PipeSecurityAttributes
{
    PACL dacl = nullptr;

    PipeSecurityAttributes() = default;
    PipeSecurityAttributes(const PipeSecurityAttributes&) = delete;
    PipeSecurityAttributes& operator=(const PipeSecurityAttributes&) = delete;
    PipeSecurityAttributes(PipeSecurityAttributes&& other) noexcept
        : dacl(other.dacl)
    {
        other.dacl = nullptr;
    }
    PipeSecurityAttributes& operator=(PipeSecurityAttributes&& other) noexcept
    {
        if (this != &other)
        {
            if (dacl)
                LocalFree(dacl);
            dacl = other.dacl;
            other.dacl = nullptr;
        }
        return *this;
    }
    ~PipeSecurityAttributes()
    {
        if (dacl)
            LocalFree(dacl);
    }
};

std::string sid_to_string(PSID sid)
{
    if (!sid)
        return {};
    LPSTR text = nullptr;
    if (!ConvertSidToStringSidA(sid, &text))
        return {};
    ScopedLocalAlloc owned_text{ text };
    return static_cast<const char*>(owned_text.ptr);
}

OwnedSid copy_sid(PSID sid)
{
    OwnedSid copy;
    if (!sid)
        return copy;
    const DWORD length = GetLengthSid(sid);
    copy.bytes.resize(length);
    if (!CopySid(length, copy.bytes.data(), sid))
        copy.bytes.clear();
    return copy;
}

OwnedSid current_user_sid()
{
    ScopedTokenHandle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.handle))
        return {};
    DWORD bytes = 0;
    GetTokenInformation(token.handle, TokenUser, nullptr, 0, &bytes);
    if (bytes == 0)
        return {};
    std::string buffer(bytes, '\0');
    if (!GetTokenInformation(token.handle, TokenUser, buffer.data(), bytes, &bytes))
        return {};
    return copy_sid(reinterpret_cast<const TOKEN_USER*>(buffer.data())->User.Sid);
}

OwnedSid current_logon_sid()
{
    ScopedTokenHandle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.handle))
        return {};
    DWORD bytes = 0;
    GetTokenInformation(token.handle, TokenGroups, nullptr, 0, &bytes);
    if (bytes == 0)
        return {};
    std::string buffer(bytes, '\0');
    if (!GetTokenInformation(token.handle, TokenGroups, buffer.data(), bytes, &bytes))
        return {};
    const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(buffer.data());
    for (DWORD i = 0; i < groups->GroupCount; ++i)
    {
        if ((groups->Groups[i].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
            return copy_sid(groups->Groups[i].Sid);
    }
    return {};
}

std::optional<PipeSecurityAttributes> make_pipe_security_attributes()
{
    OwnedSid user_sid = current_user_sid();
    OwnedSid logon_sid = current_logon_sid();
    if (user_sid.empty() && logon_sid.empty())
        return std::nullopt;

    BYTE system_sid_buffer[SECURITY_MAX_SID_SIZE] = {};
    DWORD system_sid_size = sizeof(system_sid_buffer);
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system_sid_buffer, &system_sid_size))
        return std::nullopt;
    BYTE admin_sid_buffer[SECURITY_MAX_SID_SIZE] = {};
    DWORD admin_sid_size = sizeof(admin_sid_buffer);
    if (!CreateWellKnownSid(
            WinBuiltinAdministratorsSid, nullptr, admin_sid_buffer, &admin_sid_size))
        return std::nullopt;

    struct AccessEntry
    {
        PSID sid = nullptr;
        DWORD permissions = 0;
    };
    std::vector<AccessEntry> entries{
        { system_sid_buffer, GENERIC_ALL },
        { admin_sid_buffer, GENERIC_ALL },
    };
    if (!user_sid.empty())
        entries.push_back(
            { user_sid.get(), FILE_GENERIC_READ | FILE_GENERIC_WRITE | SYNCHRONIZE });
    if (!logon_sid.empty())
        entries.push_back(
            { logon_sid.get(), FILE_GENERIC_READ | FILE_GENERIC_WRITE | SYNCHRONIZE });

    DWORD acl_size = sizeof(ACL);
    for (const auto& entry : entries)
        acl_size += sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(entry.sid) - sizeof(DWORD);

    PipeSecurityAttributes security;
    security.dacl = static_cast<PACL>(LocalAlloc(LPTR, acl_size));
    if (!security.dacl || !InitializeAcl(security.dacl, acl_size, ACL_REVISION))
        return std::nullopt;
    for (const auto& entry : entries)
    {
        if (!AddAccessAllowedAceEx(
                security.dacl, ACL_REVISION, 0, entry.permissions, entry.sid))
            return std::nullopt;
    }
    if (!IsValidAcl(security.dacl))
        return std::nullopt;

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Using explicit session attach pipe security (user_sid=%s, logon_sid=%s)",
        sid_to_string(user_sid.get()).c_str(),
        sid_to_string(logon_sid.get()).c_str());
    return security;
}

bool apply_pipe_dacl(HANDLE pipe, PACL dacl)
{
    if (!dacl)
        return true;
    SECURITY_DESCRIPTOR descriptor = {};
    return InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION)
        && SetSecurityDescriptorDacl(&descriptor, TRUE, dacl, FALSE)
        && SetKernelObjectSecurity(pipe, DACL_SECURITY_INFORMATION, &descriptor);
}

bool apply_pipe_medium_integrity_label(HANDLE pipe)
{
    ScopedSid integrity_sid;
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_MANDATORY_LABEL_AUTHORITY;
    if (!AllocateAndInitializeSid(&authority,
            1,
            SECURITY_MANDATORY_MEDIUM_RID,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            &integrity_sid.sid))
        return false;

    const DWORD sacl_size = sizeof(ACL) + sizeof(SYSTEM_MANDATORY_LABEL_ACE)
        + GetLengthSid(integrity_sid.sid) - sizeof(DWORD);
    PACL sacl = static_cast<PACL>(LocalAlloc(LPTR, sacl_size));
    if (!sacl)
        return false;
    ScopedLocalAlloc owned_sacl{ sacl };
    if (!InitializeAcl(sacl, sacl_size, ACL_REVISION)
        || !AddMandatoryAce(
            sacl, ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, integrity_sid.sid))
        return false;

    SECURITY_DESCRIPTOR descriptor = {};
    return InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION)
        && SetSecurityDescriptorSacl(&descriptor, TRUE, sacl, FALSE)
        && SetKernelObjectSecurity(pipe, LABEL_SECURITY_INFORMATION, &descriptor);
}

bool session_mutex_exists(std::string_view session_id)
{
    HANDLE mutex = OpenMutexA(SYNCHRONIZE, FALSE, mutex_name(session_id).c_str());
    if (!mutex)
        return false;
    CloseHandle(mutex);
    return true;
}

bool wait_for_pipe_server(std::string_view session_id, DWORD timeout_ms, DWORD* wait_error)
{
    const std::string name = pipe_name(session_id);
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    for (;;)
    {
        const ULONGLONG now = GetTickCount64();
        const DWORD slice = static_cast<DWORD>(
            std::min<ULONGLONG>(200, now < deadline ? deadline - now : 0));
        if (WaitNamedPipeA(name.c_str(), slice))
            return true;

        const DWORD last_error = GetLastError();
        const bool claimed = session_mutex_exists(session_id);
        if ((last_error == ERROR_FILE_NOT_FOUND || last_error == ERROR_SEM_TIMEOUT)
            && claimed && GetTickCount64() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (wait_error)
            *wait_error = last_error;
        return false;
    }
}

HANDLE open_pipe_client(
    std::string_view session_id, DWORD timeout_ms, DWORD* open_error, bool* no_server)
{
    const std::string actual_id = session_id.empty() ? "default" : std::string(session_id);
    const std::string name = pipe_name(actual_id);
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    DWORD last_error = ERROR_SEM_TIMEOUT;
    if (no_server)
        *no_server = false;

    for (;;)
    {
        const ULONGLONG now = GetTickCount64();
        const DWORD remaining = static_cast<DWORD>(
            std::min<ULONGLONG>(200, now < deadline ? deadline - now : 0));
        if (remaining == 0)
            break;

        DWORD wait_error = ERROR_SUCCESS;
        if (!wait_for_pipe_server(actual_id, remaining, &wait_error))
        {
            last_error = wait_error;
            if (wait_error == ERROR_FILE_NOT_FOUND)
            {
                if (no_server)
                    *no_server = true;
                break;
            }
            if (wait_error == ERROR_SEM_TIMEOUT)
                continue;
            break;
        }

        HANDLE pipe = CreateFileA(
            name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE)
            return pipe;
        last_error = GetLastError();
        if (last_error == ERROR_PIPE_BUSY || last_error == ERROR_SEM_TIMEOUT
            || last_error == ERROR_FILE_NOT_FOUND)
        {
            if (last_error == ERROR_FILE_NOT_FOUND && no_server)
                *no_server = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        break;
    }
    if (open_error)
        *open_error = last_error;
    return INVALID_HANDLE_VALUE;
}

class WinConnection final : public SessionConnection
{
public:
    WinConnection(HANDLE pipe, bool server_side)
        : pipe_(pipe)
        , server_side_(server_side)
    {
    }

    ~WinConnection() override
    {
        if (pipe_ == INVALID_HANDLE_VALUE)
            return;
        if (server_side_)
        {
            FlushFileBuffers(pipe_);
            DisconnectNamedPipe(pipe_);
        }
        CloseHandle(pipe_);
    }

    bool read_request(std::string* request, std::string* error) override
    {
        char buffer[kMaxRequestBytes] = {};
        DWORD bytes_read = 0;
        if (!ReadFile(pipe_, buffer, sizeof(buffer), &bytes_read, nullptr))
        {
            if (error)
                *error = "Failed reading session-attach request: "
                    + win32_error_message(GetLastError());
            return false;
        }
        if (request)
            request->assign(buffer, bytes_read);
        return bytes_read > 0;
    }

    bool write_all(std::string_view payload, std::string* error) override
    {
        DWORD bytes_written = 0;
        if (!WriteFile(pipe_,
                payload.data(),
                static_cast<DWORD>(payload.size()),
                &bytes_written,
                nullptr)
            || bytes_written != payload.size())
        {
            if (error)
                *error = "Failed writing session-attach payload: "
                    + win32_error_message(GetLastError());
            return false;
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
            DWORD bytes_read = 0;
            const BOOL ok = ReadFile(pipe_, buffer, sizeof(buffer), &bytes_read, nullptr);
            if (bytes_read > 0 && response)
                response->append(buffer, buffer + bytes_read);
            if (ok)
                continue;
            const DWORD read_error = GetLastError();
            if (read_error == ERROR_BROKEN_PIPE || read_error == ERROR_PIPE_NOT_CONNECTED)
                return true;
            if (read_error == ERROR_MORE_DATA)
                continue;
            if (error)
                *error = "Failed reading session-attach response: "
                    + win32_error_message(read_error);
            return false;
        }
    }

private:
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    bool server_side_ = false;
};

class WinTransport final : public SessionTransport
{
public:
    explicit WinTransport(std::string_view session_id)
        : session_id_(session_id.empty() ? "default" : session_id)
    {
    }

    ~WinTransport() override { close(); }

    bool start(std::string* error) override
    {
        instance_mutex_ = CreateMutexA(nullptr, FALSE, mutex_name(session_id_).c_str());
        if (!instance_mutex_)
        {
            if (error)
                *error = "Failed to create session-attach mutex: "
                    + win32_error_message(GetLastError());
            return false;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            DWORD wait_error = ERROR_SUCCESS;
            if (wait_for_pipe_server(session_id_, 2000, &wait_error))
            {
                if (error)
                    *error = "Another Draxul session-attach server is already running.";
                close();
                return false;
            }
            if (wait_error != ERROR_FILE_NOT_FOUND && wait_error != ERROR_SEM_TIMEOUT)
            {
                if (error)
                    *error = "Failed waiting for competing session-attach server: "
                        + win32_error_message(wait_error);
                close();
                return false;
            }
        }

        security_ = make_pipe_security_attributes();
        pending_pipe_ = create_server_pipe();
        if (pending_pipe_ == INVALID_HANDLE_VALUE)
        {
            if (error)
                *error = "Failed to create the session-attach pipe: "
                    + win32_error_message(GetLastError());
            close();
            return false;
        }
        return true;
    }

    std::unique_ptr<SessionConnection> accept(std::string* error) override
    {
        HANDLE pipe = pending_pipe_;
        pending_pipe_ = INVALID_HANDLE_VALUE;
        if (pipe == INVALID_HANDLE_VALUE)
            pipe = create_server_pipe();
        if (pipe == INVALID_HANDLE_VALUE)
        {
            if (error)
                *error = win32_error_message(GetLastError());
            return {};
        }

        const BOOL connected = ConnectNamedPipe(pipe, nullptr)
            ? TRUE
            : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected)
        {
            const DWORD connect_error = GetLastError();
            CloseHandle(pipe);
            if (error)
                *error = win32_error_message(connect_error);
            return {};
        }
        return std::make_unique<WinConnection>(pipe, true);
    }

    TransportResult connect(std::unique_ptr<SessionConnection>* connection,
        std::chrono::milliseconds timeout) override
    {
        DWORD open_error = ERROR_SUCCESS;
        bool no_server = false;
        HANDLE pipe = open_pipe_client(
            session_id_, static_cast<DWORD>(timeout.count()), &open_error, &no_server);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            if (no_server)
                return { TransportStatus::NoServer, {} };
            return { TransportStatus::Error,
                "Failed opening session-attach pipe: " + win32_error_message(open_error) };
        }
        if (connection)
            *connection = std::make_unique<WinConnection>(pipe, false);
        else
            CloseHandle(pipe);
        return {};
    }

    TransportResult probe(std::chrono::milliseconds timeout) override
    {
        DWORD wait_error = ERROR_SUCCESS;
        if (wait_for_pipe_server(
                session_id_, static_cast<DWORD>(timeout.count()), &wait_error))
            return {};
        if (wait_error == ERROR_FILE_NOT_FOUND)
            return { TransportStatus::NoServer, {} };
        return { TransportStatus::Error,
            "Failed waiting for session-attach pipe: " + win32_error_message(wait_error) };
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
        if (pending_pipe_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(pending_pipe_);
            pending_pipe_ = INVALID_HANDLE_VALUE;
        }
        security_.reset();
        if (instance_mutex_)
        {
            CloseHandle(instance_mutex_);
            instance_mutex_ = nullptr;
        }
    }

private:
    HANDLE create_server_pipe()
    {
        HANDLE pipe = CreateNamedPipeA(pipe_name(session_id_).c_str(),
            PIPE_ACCESS_DUPLEX | WRITE_DAC | WRITE_OWNER,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1,
            256,
            256,
            0,
            nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
            return pipe;
        if (security_ && !apply_pipe_dacl(pipe, security_->dacl))
            DRAXUL_LOG_WARN(LogCategory::App, "Failed to apply session attach pipe DACL.");
        if (!apply_pipe_medium_integrity_label(pipe))
            DRAXUL_LOG_WARN(LogCategory::App, "Failed to apply session attach pipe integrity label.");
        return pipe;
    }

    std::string session_id_;
    HANDLE instance_mutex_ = nullptr;
    HANDLE pending_pipe_ = INVALID_HANDLE_VALUE;
    std::optional<PipeSecurityAttributes> security_;
};

} // namespace

std::unique_ptr<SessionTransport> make_session_transport(std::string_view session_id)
{
    return std::make_unique<WinTransport>(session_id);
}

} // namespace draxul::session_attach_detail
