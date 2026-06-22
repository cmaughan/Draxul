#include <draxul/session_attach.h>

#include <draxul/config_document.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>

#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <optional>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <aclapi.h>
#include <sddl.h>
#include <windows.h>
#pragma comment(lib, "Advapi32.lib")
#else
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace draxul
{

namespace
{

constexpr std::string_view kRenameSessionPrefix = "rename-session:";
constexpr std::string_view kStopInternalCommand = "stop-internal";

uint64_t fnv1a_hash(std::string_view text)
{
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : text)
    {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string endpoint_suffix(std::string_view session_id)
{
    std::ostringstream out;
    out << std::hex << fnv1a_hash(ConfigDocument::default_path().parent_path().string() + "|" + std::string(session_id));
    return out.str();
}

#ifdef _WIN32

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
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD size = FormatMessageA(flags, nullptr, error, 0, reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string message = size > 0 && buffer ? std::string(buffer, size) : ("Win32 error " + std::to_string(error));
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

    bool empty() const
    {
        return bytes.empty();
    }

    PSID get()
    {
        return bytes.empty() ? nullptr : static_cast<PSID>(bytes.data());
    }

    PSID get() const
    {
        return bytes.empty() ? nullptr : const_cast<BYTE*>(bytes.data());
    }
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
        if (this == &other)
            return *this;
        if (dacl)
            LocalFree(dacl);
        dacl = other.dacl;
        other.dacl = nullptr;
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

    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    return copy_sid(user->User.Sid);
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

    PipeSecurityAttributes security;
    BYTE system_sid_buffer[SECURITY_MAX_SID_SIZE] = {};
    DWORD system_sid_size = sizeof(system_sid_buffer);
    if (!CreateWellKnownSid(
            WinLocalSystemSid, nullptr, system_sid_buffer, &system_sid_size))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to build LocalSystem SID for session attach pipe security: %s",
            win32_error_message(GetLastError()).c_str());
        return std::nullopt;
    }

    BYTE admin_sid_buffer[SECURITY_MAX_SID_SIZE] = {};
    DWORD admin_sid_size = sizeof(admin_sid_buffer);
    if (!CreateWellKnownSid(
            WinBuiltinAdministratorsSid, nullptr, admin_sid_buffer, &admin_sid_size))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to build Administrators SID for session attach pipe security: %s",
            win32_error_message(GetLastError()).c_str());
        return std::nullopt;
    }

    struct AccessEntry
    {
        PSID sid = nullptr;
        DWORD permissions = 0;
    };

    std::vector<AccessEntry> entries;
    entries.push_back({ system_sid_buffer, GENERIC_ALL });
    entries.push_back({ admin_sid_buffer, GENERIC_ALL });
    if (!user_sid.empty())
        entries.push_back({ user_sid.get(), FILE_GENERIC_READ | FILE_GENERIC_WRITE | SYNCHRONIZE });
    if (!logon_sid.empty())
        entries.push_back({ logon_sid.get(), FILE_GENERIC_READ | FILE_GENERIC_WRITE | SYNCHRONIZE });

    DWORD acl_size = sizeof(ACL);
    for (const auto& entry : entries)
        acl_size += sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(entry.sid) - sizeof(DWORD);

    security.dacl = static_cast<PACL>(LocalAlloc(LPTR, acl_size));
    if (!security.dacl)
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to allocate session attach pipe DACL.");
        return std::nullopt;
    }
    if (!InitializeAcl(security.dacl, acl_size, ACL_REVISION))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to initialize session attach pipe DACL: %s",
            win32_error_message(GetLastError()).c_str());
        return std::nullopt;
    }
    for (const auto& entry : entries)
    {
        if (!AddAccessAllowedAceEx(security.dacl, ACL_REVISION, 0, entry.permissions, entry.sid))
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "Failed to add ACE to session attach pipe DACL: %s",
                win32_error_message(GetLastError()).c_str());
            return std::nullopt;
        }
    }
    if (!IsValidAcl(security.dacl))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Session attach pipe DACL is invalid before applying it.");
        return std::nullopt;
    }

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
    if (!InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to initialize session attach pipe DACL descriptor: %s",
            win32_error_message(GetLastError()).c_str());
        return false;
    }
    if (!SetSecurityDescriptorDacl(&descriptor, TRUE, dacl, FALSE))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to build session attach pipe DACL descriptor: %s",
            win32_error_message(GetLastError()).c_str());
        return false;
    }
    if (!SetKernelObjectSecurity(pipe, DACL_SECURITY_INFORMATION, &descriptor))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to apply session attach pipe DACL: %s",
            win32_error_message(GetLastError()).c_str());
        return false;
    }

    DRAXUL_LOG_DEBUG(LogCategory::App, "Applied session attach pipe DACL.");
    return true;
}

bool apply_pipe_medium_integrity_label(HANDLE pipe)
{
    ScopedSid integrity_sid;
    SID_IDENTIFIER_AUTHORITY mandatory_label_authority = SECURITY_MANDATORY_LABEL_AUTHORITY;
    if (!AllocateAndInitializeSid(&mandatory_label_authority,
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
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to build medium-integrity SID for session attach pipe label: %s",
            win32_error_message(GetLastError()).c_str());
        return false;
    }

    const DWORD sacl_size = sizeof(ACL)
        + sizeof(SYSTEM_MANDATORY_LABEL_ACE)
        + GetLengthSid(integrity_sid.sid)
        - sizeof(DWORD);
    PACL sacl = static_cast<PACL>(LocalAlloc(LPTR, sacl_size));
    if (!sacl)
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to allocate session attach pipe label SACL.");
        return false;
    }
    ScopedLocalAlloc owned_sacl{ sacl };
    if (!InitializeAcl(sacl, sacl_size, ACL_REVISION))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to initialize session attach pipe label SACL: %s",
            win32_error_message(GetLastError()).c_str());
        return false;
    }
    if (!AddMandatoryAce(
            sacl, ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, integrity_sid.sid))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to add medium-integrity label to session attach pipe: %s",
            win32_error_message(GetLastError()).c_str());
        return false;
    }

    SECURITY_DESCRIPTOR descriptor = {};
    if (!InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to initialize session attach pipe label descriptor: %s",
            win32_error_message(GetLastError()).c_str());
        return false;
    }
    if (!SetSecurityDescriptorSacl(&descriptor, TRUE, sacl, FALSE))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to build session attach pipe label descriptor: %s",
            win32_error_message(GetLastError()).c_str());
        return false;
    }
    if (!SetKernelObjectSecurity(pipe, LABEL_SECURITY_INFORMATION, &descriptor))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to apply medium-integrity label to session attach pipe: %s",
            win32_error_message(GetLastError()).c_str());
        return false;
    }

    DRAXUL_LOG_DEBUG(LogCategory::App, "Applied medium-integrity label to session attach pipe.");
    return true;
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
        const DWORD slice = static_cast<DWORD>(std::min<ULONGLONG>(
            200, now < deadline ? (deadline - now) : 0));
        if (WaitNamedPipeA(name.c_str(), slice))
            return true;

        const DWORD error = GetLastError();
        const bool another_instance_claimed = session_mutex_exists(session_id);
        if ((error == ERROR_FILE_NOT_FOUND || error == ERROR_SEM_TIMEOUT)
            && another_instance_claimed
            && GetTickCount64() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (wait_error)
            *wait_error = error;
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "Session attach pipe wait for '%s' failed after %lu ms: %s (mutex_exists=%d)",
            std::string(session_id).c_str(),
            static_cast<unsigned long>(timeout_ms),
            win32_error_message(error).c_str(),
            another_instance_claimed ? 1 : 0);
        return false;
    }
}

HANDLE open_pipe_client(std::string_view session_id, DWORD timeout_ms, DWORD* open_error, bool* no_server)
{
    const std::string actual_session_id = session_id.empty() ? "default" : std::string(session_id);
    const std::string name = pipe_name(actual_session_id);
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    DWORD last_error = ERROR_SEM_TIMEOUT;
    int attempts = 0;

    if (no_server)
        *no_server = false;

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Opening session attach pipe for '%s' with timeout %lu ms",
        actual_session_id.c_str(),
        static_cast<unsigned long>(timeout_ms));

    for (;;)
    {
        const ULONGLONG now = GetTickCount64();
        const DWORD remaining = static_cast<DWORD>(std::min<ULONGLONG>(
            200, now < deadline ? (deadline - now) : 0));
        if (remaining == 0)
            break;
        ++attempts;

        DWORD wait_error = ERROR_SUCCESS;
        if (!wait_for_pipe_server(actual_session_id, remaining, &wait_error))
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

        HANDLE pipe
            = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE)
        {
            DRAXUL_LOG_DEBUG(LogCategory::App,
                "Opened session attach pipe for '%s' after %d attempts",
                actual_session_id.c_str(),
                attempts);
            return pipe;
        }

        last_error = GetLastError();
        if (last_error == ERROR_PIPE_BUSY || last_error == ERROR_SEM_TIMEOUT || last_error == ERROR_FILE_NOT_FOUND)
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
    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Opening session attach pipe for '%s' failed after %d attempts: %s (no_server=%d)",
        actual_session_id.c_str(),
        attempts,
        win32_error_message(last_error).c_str(),
        no_server && *no_server ? 1 : 0);
    return INVALID_HANDLE_VALUE;
}

bool send_internal_pipe_command(std::string_view session_id, std::string_view command)
{
    DWORD open_error = ERROR_SUCCESS;
    bool no_server = false;
    HANDLE pipe = open_pipe_client(session_id, 5000, &open_error, &no_server);
    if (pipe == INVALID_HANDLE_VALUE)
        return false;

    DWORD bytes_written = 0;
    const BOOL ok = WriteFile(
        pipe, command.data(), static_cast<DWORD>(command.size()), &bytes_written, nullptr);
    CloseHandle(pipe);
    return ok && bytes_written == command.size();
}

#else

std::string socket_path(std::string_view session_id)
{
    return (std::filesystem::temp_directory_path()
        / ("draxul-session-attach-" + endpoint_suffix(session_id) + ".sock"))
        .string();
}

std::string errno_message(int error)
{
    return std::strerror(error);
}

#endif

const char* command_text(SessionAttachServer::Command command)
{
    switch (command)
    {
    case SessionAttachServer::Command::Activate:
        return "activate";
    case SessionAttachServer::Command::Detach:
        return "detach";
    case SessionAttachServer::Command::Shutdown:
        return "shutdown";
    case SessionAttachServer::Command::QueryLiveSession:
        return "query-live-session";
    }
    return "activate";
}

std::string serialize_live_session_info(const SessionAttachServer::LiveSessionInfo& info)
{
    std::ostringstream out;
    out << "workspace_count=" << info.workspace_count << '\n';
    out << "pane_count=" << info.pane_count << '\n';
    out << "detached=" << (info.detached ? 1 : 0) << '\n';
    out << "owner_pid=" << info.owner_pid << '\n';
    out << "last_attached_unix_s=" << info.last_attached_unix_s << '\n';
    out << "last_detached_unix_s=" << info.last_detached_unix_s << '\n';
    return out.str();
}

template <typename T>
bool parse_integral(std::string_view text, T* value)
{
    T parsed{};
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end)
        return false;
    *value = parsed;
    return true;
}

bool parse_live_session_info(std::string_view payload,
    SessionAttachServer::LiveSessionInfo* info, std::string* error)
{
    SessionAttachServer::LiveSessionInfo parsed;
    while (!payload.empty())
    {
        const size_t newline = payload.find('\n');
        const std::string_view line = newline == std::string_view::npos
            ? payload
            : payload.substr(0, newline);
        payload = newline == std::string_view::npos ? std::string_view{} : payload.substr(newline + 1);
        if (line.empty())
            continue;

        const size_t equals = line.find('=');
        if (equals == std::string_view::npos)
        {
            if (error)
                *error = "Malformed live-session response line.";
            return false;
        }

        const std::string_view key = line.substr(0, equals);
        const std::string_view value = line.substr(equals + 1);
        if (key == "workspace_count")
        {
            if (!parse_integral(value, &parsed.workspace_count))
                return false;
        }
        else if (key == "pane_count")
        {
            if (!parse_integral(value, &parsed.pane_count))
                return false;
        }
        else if (key == "detached")
        {
            if (value == "1" || value == "true")
                parsed.detached = true;
            else if (value == "0" || value == "false")
                parsed.detached = false;
            else
                return false;
        }
        else if (key == "owner_pid")
        {
            if (!parse_integral(value, &parsed.owner_pid))
                return false;
        }
        else if (key == "last_attached_unix_s")
        {
            if (!parse_integral(value, &parsed.last_attached_unix_s))
                return false;
        }
        else if (key == "last_detached_unix_s")
        {
            if (!parse_integral(value, &parsed.last_detached_unix_s))
                return false;
        }
    }

    if (info)
        *info = parsed;
    return true;
}

} // namespace

SessionAttachServer::~SessionAttachServer()
{
    stop();
}

bool SessionAttachServer::start(
    std::string_view session_id, CommandHandler on_command_requested, std::string* error)
{
    return start(session_id, std::move(on_command_requested), QueryHandler{}, RenameHandler{}, error);
}

bool SessionAttachServer::start(std::string_view session_id, CommandHandler on_command_requested,
    QueryHandler on_query_requested, std::string* error)
{
    return start(session_id,
        std::move(on_command_requested),
        std::move(on_query_requested),
        RenameHandler{},
        error);
}

bool SessionAttachServer::start(std::string_view session_id, CommandHandler on_command_requested,
    QueryHandler on_query_requested, RenameHandler on_rename_requested, std::string* error)
{
    PERF_MEASURE();
    stop();

    session_id_ = session_id.empty() ? "default" : std::string(session_id);
    on_command_requested_ = std::move(on_command_requested);
    on_query_requested_ = std::move(on_query_requested);
    on_rename_requested_ = std::move(on_rename_requested);
    stop_requested_ = false;

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Starting session attach server for '%s'",
        session_id_.c_str());

#ifdef _WIN32
    HANDLE mutex = CreateMutexA(nullptr, FALSE, mutex_name(session_id_).c_str());
    if (!mutex)
    {
        if (error)
            *error = "Failed to create session-attach mutex: " + win32_error_message(GetLastError());
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        // Another process holds the mutex. Check whether its pipe server is
        // actually alive. If the pipe is gone the old server thread has died
        // (crash, stuck process, etc.) and we should take over the session
        // rather than refusing to start.
        DWORD wait_error = ERROR_SUCCESS;
        if (wait_for_pipe_server(session_id_, 2000, &wait_error))
        {
            // Pipe is alive — another server is truly running.
            CloseHandle(mutex);
            if (error)
                *error = "Another Draxul session-attach server is already running.";
            return false;
        }
        if (wait_error != ERROR_FILE_NOT_FOUND && wait_error != ERROR_SEM_TIMEOUT)
        {
            CloseHandle(mutex);
            if (error)
                *error = "Failed waiting for competing session-attach server: " + win32_error_message(wait_error);
            return false;
        }
        // Pipe is still absent after a grace period — take over. Keep the
        // mutex handle and proceed.
        DRAXUL_LOG_WARN(LogCategory::App,
            "Session-attach mutex exists but no pipe appeared after startup grace — taking over session '%s'",
            session_id_.c_str());
    }
    instance_mutex_ = mutex;

    auto pipe_security = make_pipe_security_attributes();
    if (!pipe_security)
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Falling back to default session attach pipe security for '%s'",
            session_id_.c_str());
    }

    std::promise<bool> ready_promise;
    auto ready_future = ready_promise.get_future();
    server_thread_ = std::thread([this, ready = std::move(ready_promise),
                                     pipe_security = std::move(pipe_security)]() mutable {
        const std::string name = pipe_name(session_id_);
        bool ready_signaled = false;
        while (!stop_requested_.load())
        {
            HANDLE pipe = CreateNamedPipeA(name.c_str(),
                PIPE_ACCESS_DUPLEX | WRITE_DAC | WRITE_OWNER,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                1,
                256,
                256,
                0,
                nullptr);
            if (pipe == INVALID_HANDLE_VALUE)
            {
                if (!ready_signaled)
                {
                    ready.set_value(false);
                    ready_signaled = true;
                }
                DRAXUL_LOG_ERROR(LogCategory::App,
                    "Session attach server failed to create pipe: %s",
                    win32_error_message(GetLastError()).c_str());
                break;
            }
            if (pipe_security)
                (void)apply_pipe_dacl(pipe, pipe_security->dacl);
            (void)apply_pipe_medium_integrity_label(pipe);
            if (!ready_signaled)
            {
                ready.set_value(true);
                ready_signaled = true;
                DRAXUL_LOG_DEBUG(LogCategory::App,
                    "Session attach server pipe ready for '%s'",
                    session_id_.c_str());
            }

            const BOOL connected = ConnectNamedPipe(pipe, nullptr)
                ? TRUE
                : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (!connected)
            {
                CloseHandle(pipe);
                if (!stop_requested_.load())
                {
                    DRAXUL_LOG_WARN(LogCategory::App,
                        "Session attach server connect failed: %s",
                        win32_error_message(GetLastError()).c_str());
                }
                continue;
            }

            char buffer[1024] = {};
            DWORD bytes_read = 0;
            const BOOL read_ok = ReadFile(pipe, buffer, sizeof(buffer), &bytes_read, nullptr);
            std::string response = "ok";
            if (read_ok && bytes_read > 0)
            {
                const std::string_view command(buffer, bytes_read);
                DRAXUL_LOG_DEBUG(LogCategory::App,
                    "Session attach server for '%s' received command '%.*s'",
                    session_id_.c_str(),
                    static_cast<int>(command.size()),
                    command.data());
                if (command == kStopInternalCommand)
                {
                    stop_requested_ = true;
                }
                else if (!stop_requested_.load() && command == "activate")
                {
                    if (on_command_requested_)
                        on_command_requested_(Command::Activate);
                }
                else if (!stop_requested_.load() && command.starts_with(kRenameSessionPrefix))
                {
                    if (on_rename_requested_)
                        on_rename_requested_(command.substr(kRenameSessionPrefix.size()));
                }
                else if (!stop_requested_.load() && command == "detach")
                {
                    if (on_command_requested_)
                        on_command_requested_(Command::Detach);
                }
                else if (!stop_requested_.load() && command == "shutdown")
                {
                    if (on_command_requested_)
                        on_command_requested_(Command::Shutdown);
                    stop_requested_ = true;
                }
                else if (!stop_requested_.load() && command == "query-live-session")
                {
                    response = serialize_live_session_info(
                        on_query_requested_ ? on_query_requested_() : LiveSessionInfo{});
                }
            }

            if (read_ok && bytes_read > 0)
            {
                DWORD bytes_written = 0;
                (void)WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &bytes_written, nullptr);
            }

            FlushFileBuffers(pipe);
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }

        running_ = false;
    });
    if (!ready_future.get())
    {
        if (server_thread_.joinable())
            server_thread_.join();
        if (instance_mutex_)
        {
            CloseHandle(static_cast<HANDLE>(instance_mutex_));
            instance_mutex_ = nullptr;
        }
        if (error)
            *error = "Failed to create the session-attach pipe.";
        return false;
    }
    running_ = true;
    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Session attach server started for '%s'",
        session_id_.c_str());
#else
    socket_path_ = socket_path(session_id_);
    const int probe_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (probe_fd >= 0)
    {
        sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path_.c_str());
        if (::connect(probe_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            if (errno == ENOENT || errno == ECONNREFUSED)
            {
                std::error_code ec;
                std::filesystem::remove(socket_path_, ec);
            }
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

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path_.c_str());
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        if (error)
            *error = "Failed to bind session-attach socket: " + errno_message(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    if (::listen(listen_fd_, 4) != 0)
    {
        if (error)
            *error = "Failed to listen on session-attach socket: " + errno_message(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        std::error_code ec;
        std::filesystem::remove(socket_path_, ec);
        return false;
    }

    running_ = true;
    server_thread_ = std::thread([this]() {
        while (!stop_requested_.load())
        {
            const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
            if (client_fd < 0)
            {
                if (!stop_requested_.load())
                {
                    DRAXUL_LOG_WARN(LogCategory::App,
                        "Session attach server accept failed: %s",
                        errno_message(errno).c_str());
                }
                continue;
            }

            char buffer[1024] = {};
            const ssize_t bytes_read = ::read(client_fd, buffer, sizeof(buffer));
            std::string response = "ok";
            if (bytes_read > 0)
            {
                const std::string_view command(buffer, static_cast<size_t>(bytes_read));
                if (command == kStopInternalCommand)
                {
                    stop_requested_ = true;
                }
                else if (!stop_requested_.load() && command == "activate")
                {
                    if (on_command_requested_)
                        on_command_requested_(Command::Activate);
                }
                else if (!stop_requested_.load() && command.starts_with(kRenameSessionPrefix))
                {
                    if (on_rename_requested_)
                        on_rename_requested_(command.substr(kRenameSessionPrefix.size()));
                }
                else if (!stop_requested_.load() && command == "detach")
                {
                    if (on_command_requested_)
                        on_command_requested_(Command::Detach);
                }
                else if (!stop_requested_.load() && command == "shutdown")
                {
                    if (on_command_requested_)
                        on_command_requested_(Command::Shutdown);
                    stop_requested_ = true;
                }
                else if (!stop_requested_.load() && command == "query-live-session")
                {
                    response = serialize_live_session_info(
                        on_query_requested_ ? on_query_requested_() : LiveSessionInfo{});
                }
            }
            if (bytes_read > 0)
                (void)::write(client_fd, response.data(), response.size());
            ::close(client_fd);
        }

        running_ = false;
    });
#endif

    return true;
}

void SessionAttachServer::stop()
{
    PERF_MEASURE();
    stop_requested_ = true;

#ifdef _WIN32
    if (running())
    {
        (void)send_internal_pipe_command(session_id_, kStopInternalCommand);
    }
#else
    if (listen_fd_ >= 0)
    {
        const int wake_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (wake_fd >= 0)
        {
            sockaddr_un addr = {};
            addr.sun_family = AF_UNIX;
            std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path_.c_str());
            (void)::connect(wake_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
            (void)::write(wake_fd, kStopInternalCommand.data(), kStopInternalCommand.size());
            ::close(wake_fd);
        }
    }
#endif

    if (server_thread_.joinable())
        server_thread_.join();

#ifdef _WIN32
    if (instance_mutex_)
    {
        CloseHandle(static_cast<HANDLE>(instance_mutex_));
        instance_mutex_ = nullptr;
    }
#else
    if (listen_fd_ >= 0)
    {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!socket_path_.empty())
    {
        std::error_code ec;
        std::filesystem::remove(socket_path_, ec);
        socket_path_.clear();
    }
#endif

    running_ = false;
}

SessionAttachServer::ProbeStatus SessionAttachServer::probe(
    std::string_view session_id, std::string* error)
{
    PERF_MEASURE();
#ifdef _WIN32
    const std::string actual_session_id = session_id.empty() ? "default" : std::string(session_id);
    DWORD wait_error = ERROR_SUCCESS;
    if (!wait_for_pipe_server(actual_session_id, 5000, &wait_error))
    {
        if (wait_error == ERROR_FILE_NOT_FOUND)
            return ProbeStatus::NoServer;
        if (error)
            *error = "Failed waiting for session-attach pipe: " + win32_error_message(wait_error);
        return ProbeStatus::Error;
    }
    return ProbeStatus::Running;
#else
    const std::string path = socket_path(session_id.empty() ? "default" : session_id);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        if (error)
            *error = "Failed creating session-attach client socket: " + errno_message(errno);
        return ProbeStatus::Error;
    }

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        const int connect_error = errno;
        ::close(fd);
        if (connect_error == ENOENT || connect_error == ECONNREFUSED)
            return ProbeStatus::NoServer;
        if (error)
            *error = "Failed connecting to session-attach socket: " + errno_message(connect_error);
        return ProbeStatus::Error;
    }
    ::close(fd);
    return ProbeStatus::Running;
#endif
}

SessionAttachServer::AttachStatus SessionAttachServer::try_attach(
    std::string_view session_id, std::string* error)
{
    return send_command(session_id, Command::Activate, error);
}

SessionAttachServer::AttachStatus SessionAttachServer::send_command(
    std::string_view session_id, Command command, std::string* error)
{
    PERF_MEASURE();
#ifdef _WIN32
    const std::string actual_session_id = session_id.empty() ? "default" : std::string(session_id);
    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Sending session attach command '%s' to '%s'",
        command_text(command),
        actual_session_id.c_str());
    DWORD open_error = ERROR_SUCCESS;
    bool no_server = false;
    HANDLE pipe = open_pipe_client(session_id, 5000, &open_error, &no_server);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "Session attach command '%s' to '%s' could not open pipe (no_server=%d, error=%s)",
            command_text(command),
            actual_session_id.c_str(),
            no_server ? 1 : 0,
            win32_error_message(open_error).c_str());
        if (no_server)
            return AttachStatus::NoServer;
        if (error)
            *error = "Failed opening session-attach pipe: " + win32_error_message(open_error);
        return AttachStatus::Error;
    }
    DWORD bytes_written = 0;
    const char* command_name = command_text(command);
    const BOOL ok = WriteFile(
        pipe, command_name, static_cast<DWORD>(std::strlen(command_name)), &bytes_written, nullptr);
    CloseHandle(pipe);
    if (!ok)
    {
        if (error)
            *error = "Failed writing session-attach command: " + win32_error_message(GetLastError());
        return AttachStatus::Error;
    }
    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Session attach command '%s' sent to '%s'",
        command_text(command),
        actual_session_id.c_str());
    return AttachStatus::Attached;
#else
    const std::string path = socket_path(session_id.empty() ? "default" : session_id);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        if (error)
            *error = "Failed creating session-attach client socket: " + errno_message(errno);
        return AttachStatus::Error;
    }

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        const int connect_error = errno;
        ::close(fd);
        if (connect_error == ENOENT)
            return AttachStatus::NoServer;
        if (error)
            *error = "Failed connecting to session-attach socket: " + errno_message(connect_error);
        return AttachStatus::Error;
    }

    const char* command_name = command_text(command);
    if (::write(fd, command_name, std::strlen(command_name)) < 0)
    {
        const int write_error = errno;
        ::close(fd);
        if (error)
            *error = "Failed writing session-attach command: " + errno_message(write_error);
        return AttachStatus::Error;
    }
    ::close(fd);
    return AttachStatus::Attached;
#endif
}

bool SessionAttachServer::query_live_session(
    std::string_view session_id, LiveSessionInfo* info, std::string* error)
{
    PERF_MEASURE();
    std::string response;
#ifdef _WIN32
    DWORD open_error = ERROR_SUCCESS;
    bool no_server = false;
    HANDLE pipe = open_pipe_client(session_id, 5000, &open_error, &no_server);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        if (no_server)
            return false;
        if (error)
            *error = "Failed opening session-attach pipe: " + win32_error_message(open_error);
        return false;
    }

    DWORD bytes_written = 0;
    const char* command_name = command_text(Command::QueryLiveSession);
    if (!WriteFile(pipe, command_name, static_cast<DWORD>(std::strlen(command_name)), &bytes_written, nullptr))
    {
        if (error)
            *error = "Failed writing live-session query: " + win32_error_message(GetLastError());
        CloseHandle(pipe);
        return false;
    }

    char buffer[256];
    DWORD bytes_read = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0)
        response.append(buffer, buffer + bytes_read);
    const DWORD read_error = GetLastError();
    CloseHandle(pipe);
    if (response.empty() && read_error != ERROR_BROKEN_PIPE && read_error != ERROR_SUCCESS)
    {
        if (error)
            *error = "Failed reading live-session response: " + win32_error_message(read_error);
        return false;
    }
#else
    const std::string path = socket_path(session_id.empty() ? "default" : session_id);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        if (error)
            *error = "Failed creating session-attach client socket: " + errno_message(errno);
        return false;
    }

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        if (error)
            *error = "Failed connecting to session-attach socket: " + errno_message(errno);
        ::close(fd);
        return false;
    }

    const char* command_name = command_text(Command::QueryLiveSession);
    if (::write(fd, command_name, std::strlen(command_name)) < 0)
    {
        if (error)
            *error = "Failed writing live-session query: " + errno_message(errno);
        ::close(fd);
        return false;
    }

    char buffer[256];
    ssize_t bytes_read = 0;
    while ((bytes_read = ::read(fd, buffer, sizeof(buffer))) > 0)
        response.append(buffer, buffer + bytes_read);
    if (bytes_read < 0)
    {
        if (error)
            *error = "Failed reading live-session response: " + errno_message(errno);
        ::close(fd);
        return false;
    }
    ::close(fd);
#endif

    if (!parse_live_session_info(response, info, error))
    {
        if (error && error->empty())
            *error = "Failed to parse the live-session response.";
        return false;
    }
    return true;
}

bool SessionAttachServer::rename_session(
    std::string_view session_id, std::string_view session_name, std::string* error)
{
    PERF_MEASURE();
    if (session_name.empty())
    {
        if (error)
            *error = "Session name must not be empty.";
        return false;
    }

    const std::string request = std::string(kRenameSessionPrefix) + std::string(session_name);
    std::string response;
#ifdef _WIN32
    DWORD open_error = ERROR_SUCCESS;
    bool no_server = false;
    HANDLE pipe = open_pipe_client(session_id, 5000, &open_error, &no_server);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        if (no_server)
        {
            if (error)
                *error = "No running session.";
            return false;
        }
        if (error)
            *error = "Failed opening session-attach pipe: " + win32_error_message(open_error);
        return false;
    }

    DWORD bytes_written = 0;
    if (!WriteFile(pipe, request.data(), static_cast<DWORD>(request.size()), &bytes_written, nullptr))
    {
        if (error)
            *error = "Failed writing rename-session request: " + win32_error_message(GetLastError());
        CloseHandle(pipe);
        return false;
    }

    char buffer[256];
    DWORD bytes_read = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0)
        response.append(buffer, buffer + bytes_read);
    CloseHandle(pipe);
#else
    const std::string path = socket_path(session_id.empty() ? "default" : session_id);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        if (error)
            *error = "Failed creating session-attach client socket: " + errno_message(errno);
        return false;
    }

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        if (error)
            *error = "Failed connecting to session-attach socket: " + errno_message(errno);
        ::close(fd);
        return false;
    }

    if (::write(fd, request.data(), request.size()) < 0)
    {
        if (error)
            *error = "Failed writing rename-session request: " + errno_message(errno);
        ::close(fd);
        return false;
    }

    char buffer[256];
    ssize_t bytes_read = 0;
    while ((bytes_read = ::read(fd, buffer, sizeof(buffer))) > 0)
        response.append(buffer, buffer + bytes_read);
    ::close(fd);
#endif

    if (!response.empty() && response != "ok")
    {
        if (error)
            *error = response;
        return false;
    }
    return true;
}

} // namespace draxul
