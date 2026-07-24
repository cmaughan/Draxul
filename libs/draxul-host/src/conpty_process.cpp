#include "conpty_process.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/process_util.h>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <shellapi.h>
#include <thread>
#include <tlhelp32.h>
#include <unordered_map>
#include <unordered_set>
#include <winternl.h>

namespace draxul
{

namespace
{

std::wstring widen_utf8(std::string_view text)
{
    if (text.empty())
        return {};

    const int size = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
        return {};

    std::wstring wide(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size) <= 0)
        return {};
    return wide;
}

std::string narrow_utf8(std::wstring_view text)
{
    if (text.empty())
        return {};

    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};

    std::string utf8(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr)
        <= 0)
        return {};
    return utf8;
}

std::vector<wchar_t> build_environment_block(
    const std::vector<std::pair<std::string, std::string>>& overrides)
{
    std::vector<std::pair<std::wstring, std::wstring>> wide_overrides;
    wide_overrides.reserve(overrides.size());
    for (const auto& [key, value] : overrides)
        wide_overrides.emplace_back(widen_utf8(key), widen_utf8(value));

    std::vector<std::wstring> entries;
    LPWCH raw = GetEnvironmentStringsW();
    if (raw)
    {
        for (const wchar_t* current = raw; *current != L'\0';
             current += std::wcslen(current) + 1)
        {
            const std::wstring_view entry(current);
            const size_t key_start = entry.starts_with(L'=') ? 1 : 0;
            const size_t equals = entry.find(L'=', key_start);
            const std::wstring_view key = entry.substr(0, equals);
            const bool replaced = std::any_of(wide_overrides.begin(), wide_overrides.end(),
                [&](const auto& value) {
                    return _wcsicmp(std::wstring(key).c_str(), value.first.c_str()) == 0;
                });
            if (!replaced)
                entries.emplace_back(entry);
        }
        FreeEnvironmentStringsW(raw);
    }
    for (const auto& [key, value] : wide_overrides)
        entries.push_back(key + L"=" + value);

    std::sort(entries.begin(), entries.end(), [](const std::wstring& lhs,
                                              const std::wstring& rhs) {
        return _wcsicmp(lhs.c_str(), rhs.c_str()) < 0;
    });
    std::vector<wchar_t> block;
    for (const std::wstring& entry : entries)
    {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

bool command_looks_like_path(std::string_view command)
{
    return command.find('\\') != std::string_view::npos
        || command.find('/') != std::string_view::npos
        || command.find(':') != std::string_view::npos;
}

bool path_looks_like_windows_apps_alias(std::wstring_view path)
{
    if (path.find(L"\\WindowsApps\\") == std::wstring_view::npos)
        return false;

    std::wstring path_z(path);
    const DWORD attrs = GetFileAttributesW(path_z.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return true;

    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

BOOL CALLBACK collect_console_window(HWND hwnd, LPARAM context)
{
    auto* windows = reinterpret_cast<std::unordered_set<HWND>*>(context);
    wchar_t class_name[64] = {};
    if (GetClassNameW(hwnd, class_name, static_cast<int>(std::size(class_name)))
        && wcscmp(class_name, L"ConsoleWindowClass") == 0)
    {
        windows->insert(hwnd);
    }
    return TRUE;
}

std::unordered_set<HWND> console_window_snapshot()
{
    std::unordered_set<HWND> windows;
    EnumWindows(collect_console_window, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

struct ConsoleWindowHideContext
{
    const std::unordered_set<HWND>* existing = nullptr;
};

BOOL CALLBACK hide_new_console_window(HWND hwnd, LPARAM context)
{
    auto* hide_context = reinterpret_cast<ConsoleWindowHideContext*>(context);
    if (hide_context && hide_context->existing && hide_context->existing->contains(hwnd))
        return TRUE;

    wchar_t class_name[64] = {};
    if (!GetClassNameW(hwnd, class_name, static_cast<int>(std::size(class_name)))
        || wcscmp(class_name, L"ConsoleWindowClass") != 0)
    {
        return TRUE;
    }

    ShowWindowAsync(hwnd, SW_HIDE);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return TRUE;
}

void hide_new_console_windows_for_startup(std::unordered_set<HWND> existing)
{
    ConsoleWindowHideContext context{ &existing };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline)
    {
        EnumWindows(hide_new_console_window, reinterpret_cast<LPARAM>(&context));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::wstring resolve_application_path(std::string_view command)
{
    const std::wstring requested = widen_utf8(command);
    if (requested.empty())
        return {};

    if (command_looks_like_path(command))
        return requested;

    const bool has_extension = std::filesystem::path(requested).has_extension();
    const wchar_t* extension = has_extension ? nullptr : L".exe";
    DWORD required = SearchPathW(nullptr, requested.c_str(), extension, 0, nullptr, nullptr);
    if (required == 0)
        return {};

    std::wstring resolved(static_cast<size_t>(required), L'\0');
    required = SearchPathW(nullptr, requested.c_str(), extension,
        static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
    if (required == 0)
        return {};

    if (!resolved.empty() && resolved.back() == L'\0')
        resolved.pop_back();
    return resolved;
}

using NtQueryInformationProcessFn = NTSTATUS(WINAPI*)(
    HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

struct RemoteUnicodeString
{
    USHORT length = 0;
    USHORT maximum_length = 0;
    PWSTR buffer = nullptr;
};

struct RemoteCurrentDirectory
{
    RemoteUnicodeString dos_path;
    HANDLE handle = nullptr;
};

struct RemotePebPrefix
{
    BYTE reserved1[2] = {};
    BYTE being_debugged = 0;
    BYTE reserved2[1] = {};
    PVOID reserved3[2] = {};
    PVOID ldr = nullptr;
    PVOID process_parameters = nullptr;
};

struct RemoteProcessParametersPrefix
{
    ULONG maximum_length = 0;
    ULONG length = 0;
    ULONG flags = 0;
    ULONG debug_flags = 0;
    HANDLE console_handle = nullptr;
    ULONG console_flags = 0;
    HANDLE standard_input = nullptr;
    HANDLE standard_output = nullptr;
    HANDLE standard_error = nullptr;
    RemoteCurrentDirectory current_directory;
    RemoteUnicodeString dll_path;
    RemoteUnicodeString image_path_name;
    RemoteUnicodeString command_line;
    PVOID environment = nullptr;
};

template <typename T>
bool read_process_value(HANDLE process, const void* address, T* value)
{
    if (!process || !address || !value)
        return false;
    SIZE_T bytes_read = 0;
    return ReadProcessMemory(process, address, value, sizeof(T), &bytes_read)
        && bytes_read == sizeof(T);
}

std::string read_remote_current_directory(HANDLE process)
{
    if (!process)
        return {};

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll)
        return {};
    auto* query_info = reinterpret_cast<NtQueryInformationProcessFn>(
        GetProcAddress(ntdll, "NtQueryInformationProcess"));
    if (!query_info)
        return {};

    PROCESS_BASIC_INFORMATION basic = {};
    ULONG returned = 0;
    if (query_info(process, ProcessBasicInformation, &basic, sizeof(basic), &returned) < 0
        || !basic.PebBaseAddress)
    {
        return {};
    }

    RemotePebPrefix peb = {};
    if (!read_process_value(process, basic.PebBaseAddress, &peb) || !peb.process_parameters)
        return {};

    RemoteProcessParametersPrefix params = {};
    if (!read_process_value(process, peb.process_parameters, &params))
        return {};

    const RemoteUnicodeString& path = params.current_directory.dos_path;
    if (!path.buffer || path.length == 0 || path.length > 32768 || (path.length % sizeof(wchar_t)) != 0)
        return {};

    std::wstring wide(static_cast<size_t>(path.length / sizeof(wchar_t)), L'\0');
    SIZE_T bytes_read = 0;
    if (!ReadProcessMemory(process, path.buffer, wide.data(), path.length, &bytes_read)
        || bytes_read != path.length)
    {
        return {};
    }

    return narrow_utf8(wide);
}

bool read_remote_process_parameters(
    HANDLE process, RemoteProcessParametersPrefix* params)
{
    if (!process || !params)
        return false;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll)
        return false;
    auto* query_info = reinterpret_cast<NtQueryInformationProcessFn>(
        GetProcAddress(ntdll, "NtQueryInformationProcess"));
    if (!query_info)
        return false;
    PROCESS_BASIC_INFORMATION basic = {};
    ULONG returned = 0;
    if (query_info(process, ProcessBasicInformation, &basic, sizeof(basic),
            &returned)
            < 0
        || !basic.PebBaseAddress)
        return false;
    RemotePebPrefix peb = {};
    return read_process_value(process, basic.PebBaseAddress, &peb)
        && peb.process_parameters
        && read_process_value(process, peb.process_parameters, params);
}

std::wstring read_remote_unicode(
    HANDLE process, const RemoteUnicodeString& value, size_t max_bytes)
{
    if (!value.buffer || value.length == 0 || value.length > max_bytes
        || value.length % sizeof(wchar_t) != 0)
        return {};
    std::wstring result(value.length / sizeof(wchar_t), L'\0');
    SIZE_T bytes_read = 0;
    if (!ReadProcessMemory(process, value.buffer, result.data(), value.length,
            &bytes_read)
        || bytes_read != value.length)
        return {};
    return result;
}

void read_process_arguments_and_hint(DWORD process_id,
    std::vector<std::string>* arguments, std::string* hint)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION
            | PROCESS_VM_READ,
        FALSE, process_id);
    if (!process)
        return;
    RemoteProcessParametersPrefix params = {};
    if (!read_remote_process_parameters(process, &params))
    {
        CloseHandle(process);
        return;
    }

    const std::wstring command_line =
        read_remote_unicode(process, params.command_line, 64 * 1024);
    if (!command_line.empty() && arguments)
    {
        int count = 0;
        LPWSTR* values = CommandLineToArgvW(command_line.c_str(), &count);
        if (values)
        {
            for (int index = 0; index < count && index < 64; ++index)
                arguments->push_back(narrow_utf8(values[index]));
            LocalFree(values);
        }
    }

    if (params.environment && hint)
    {
        MEMORY_BASIC_INFORMATION region = {};
        if (VirtualQueryEx(process, params.environment, &region, sizeof(region))
            == sizeof(region))
        {
            const size_t bytes =
                std::min<size_t>(region.RegionSize, 64 * 1024);
            std::vector<wchar_t> environment(bytes / sizeof(wchar_t), L'\0');
            SIZE_T bytes_read = 0;
            if (ReadProcessMemory(process, params.environment,
                    environment.data(),
                    environment.size() * sizeof(wchar_t), &bytes_read))
            {
                const size_t count = bytes_read / sizeof(wchar_t);
                size_t offset = 0;
                while (offset < count && environment[offset] != L'\0')
                {
                    const wchar_t* entry = environment.data() + offset;
                    const size_t remaining = count - offset;
                    const size_t length = wcsnlen_s(entry, remaining);
                    constexpr std::wstring_view prefix = L"DRAXUL_AGENT=";
                    if (length >= prefix.size()
                        && _wcsnicmp(entry, prefix.data(), prefix.size()) == 0)
                    {
                        *hint = narrow_utf8(
                            std::wstring_view(entry + prefix.size(),
                                length - prefix.size()));
                        break;
                    }
                    offset += length + 1;
                }
            }
        }
    }
    CloseHandle(process);
}

std::string process_executable_name(DWORD process_id)
{
    HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process)
        return {};
    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    const bool ok = QueryFullProcessImageNameW(process, 0, path.data(), &size);
    CloseHandle(process);
    if (!ok || size == 0)
        return {};
    path.resize(size);
    return narrow_utf8(path);
}

} // namespace

ConPtyProcess::~ConPtyProcess()
{
    shutdown();
}

bool ConPtyProcess::spawn(const std::string& command, const std::vector<std::string>& args,
    const std::string& working_dir, int initial_cols, int initial_rows,
    std::function<void()> on_output_available,
    const std::vector<std::pair<std::string, std::string>>& environment)
{
    PERF_MEASURE();
    shutdown();
    last_exit_code_.reset();

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE pty_input_read = INVALID_HANDLE_VALUE;
    HANDLE pty_input_write = INVALID_HANDLE_VALUE;
    HANDLE pty_output_read = INVALID_HANDLE_VALUE;
    HANDLE pty_output_write = INVALID_HANDLE_VALUE;

    auto cleanup = [&]() {
        if (pty_input_read != INVALID_HANDLE_VALUE)
            CloseHandle(pty_input_read);
        if (pty_input_write != INVALID_HANDLE_VALUE)
            CloseHandle(pty_input_write);
        if (pty_output_read != INVALID_HANDLE_VALUE)
            CloseHandle(pty_output_read);
        if (pty_output_write != INVALID_HANDLE_VALUE)
            CloseHandle(pty_output_write);
        if (pty_)
        {
            ClosePseudoConsole(pty_);
            pty_ = nullptr;
        }
    };

    if (!CreatePipe(&pty_input_read, &pty_input_write, &sa, 0))
        return false;
    if (!CreatePipe(&pty_output_read, &pty_output_write, &sa, 0))
    {
        cleanup();
        return false;
    }

    SetHandleInformation(pty_input_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(pty_output_read, HANDLE_FLAG_INHERIT, 0);

    COORD size = {
        static_cast<SHORT>(std::clamp(initial_cols, 1, 320)),
        static_cast<SHORT>(std::clamp(initial_rows, 1, 200)),
    };
    if (FAILED(CreatePseudoConsole(size, pty_input_read, pty_output_write, 0, &pty_)))
    {
        cleanup();
        return false;
    }

    SIZE_T attribute_bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
    attribute_storage_.resize(attribute_bytes);

    STARTUPINFOEXW startup = {};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.hStdInput = nullptr;
    startup.StartupInfo.hStdOutput = nullptr;
    startup.StartupInfo.hStdError = nullptr;
    startup.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage_.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_bytes))
    {
        cleanup();
        return false;
    }
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
            pty_, sizeof(pty_), nullptr, nullptr))
    {
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        cleanup();
        return false;
    }

    std::string command_line = quote_windows_arg(command);
    for (const auto& arg : args)
        command_line += " " + quote_windows_arg(arg);
    const std::wstring command_line_w = widen_utf8(command_line);
    std::vector<wchar_t> command_line_buffer(command_line_w.begin(), command_line_w.end());
    command_line_buffer.push_back(L'\0');

    std::wstring application_path_w = resolve_application_path(command);
    if (application_path_w.empty())
        application_path_w = widen_utf8(command);
    const std::string application_path_utf8 = narrow_utf8(application_path_w);
    const std::wstring working_dir_w = widen_utf8(working_dir);

    // ConPTY child creation is supposed to use the pseudoconsole attribute with
    // EXTENDED_STARTUPINFO_PRESENT. CREATE_NO_WINDOW severs the child from the
    // console environment that ConPTY is trying to provide.
    const DWORD creation_flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;
    std::vector<wchar_t> environment_block = build_environment_block(environment);
    DRAXUL_LOG_DEBUG(LogCategory::App,
        "ConPTY spawn request: command='%s' resolved='%s' cwd='%s' cols=%d rows=%d flags=0x%08lx",
        command.c_str(),
        application_path_utf8.empty() ? command.c_str() : application_path_utf8.c_str(),
        working_dir.empty() ? "" : working_dir.c_str(),
        static_cast<int>(size.X),
        static_cast<int>(size.Y),
        static_cast<unsigned long>(creation_flags));
    if (path_looks_like_windows_apps_alias(application_path_w))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "ConPTY resolved '%s' through a WindowsApps alias: '%s'",
            command.c_str(),
            application_path_utf8.c_str());
    }

    auto existing_console_windows = console_window_snapshot();
    std::thread([existing = std::move(existing_console_windows)]() mutable {
        hide_new_console_windows_for_startup(std::move(existing));
    }).detach();

    const bool created = CreateProcessW(
        nullptr,
        command_line_buffer.data(),
        nullptr,
        nullptr,
        FALSE,
        creation_flags,
        environment_block.data(),
        working_dir_w.empty() ? nullptr : working_dir_w.c_str(),
        &startup.StartupInfo,
        &proc_info_);

    DeleteProcThreadAttributeList(startup.lpAttributeList);
    CloseHandle(pty_input_read);
    pty_input_read = INVALID_HANDLE_VALUE;
    CloseHandle(pty_output_write);
    pty_output_write = INVALID_HANDLE_VALUE;

    if (!created)
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "ConPTY CreateProcessW failed for '%s' (resolved='%s', error=%lu)",
            command.c_str(),
            application_path_utf8.empty() ? command.c_str() : application_path_utf8.c_str(),
            static_cast<unsigned long>(GetLastError()));
        cleanup();
        return false;
    }

    job_ = CreateJobObjectW(nullptr, nullptr);
    if (job_)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits = {};
        job_limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(
                job_, JobObjectExtendedLimitInformation, &job_limits, sizeof(job_limits))
            || !AssignProcessToJobObject(job_, proc_info_.hProcess))
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "ConPTY failed to attach child pid=%lu to cleanup job (error=%lu); using direct termination fallback",
                static_cast<unsigned long>(proc_info_.dwProcessId),
                static_cast<unsigned long>(GetLastError()));
            CloseHandle(job_);
            job_ = nullptr;
        }
    }
    else
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "ConPTY failed to create cleanup job for child pid=%lu (error=%lu); using direct termination fallback",
            static_cast<unsigned long>(proc_info_.dwProcessId),
            static_cast<unsigned long>(GetLastError()));
    }

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "ConPTY child started: pid=%lu command='%s' resolved='%s'",
        static_cast<unsigned long>(proc_info_.dwProcessId),
        command.c_str(),
        application_path_utf8.empty() ? command.c_str() : application_path_utf8.c_str());

    input_write_ = pty_input_write;
    output_read_ = pty_output_read;
    on_output_available_ = std::move(on_output_available);
    reader_running_ = true;
    reader_thread_ = std::thread([this]() { reader_main(); });
    return true;
}

void ConPtyProcess::shutdown()
{
    PERF_MEASURE();
    reader_running_ = false;

    if (reader_thread_.joinable())
        CancelSynchronousIo(static_cast<HANDLE>(reader_thread_.native_handle()));

    if (pty_)
    {
        ClosePseudoConsole(pty_);
        pty_ = nullptr;
    }

    if (input_write_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(input_write_);
        input_write_ = INVALID_HANDLE_VALUE;
    }
    if (output_read_ != INVALID_HANDLE_VALUE)
    {
        CancelIoEx(output_read_, nullptr);
        CloseHandle(output_read_);
        output_read_ = INVALID_HANDLE_VALUE;
    }
    if (reader_thread_.joinable())
        reader_thread_.join();

    if (proc_info_.hProcess)
    {
        // Keep teardown synchronous and bounded: a detached reaper can be
        // destroyed during app exit before it terminates the shell process.
        HANDLE process_handle = proc_info_.hProcess;
        proc_info_.hProcess = nullptr;
        DWORD exit_code = 0;
        if (GetExitCodeProcess(process_handle, &exit_code))
        {
            if (exit_code == STILL_ACTIVE)
            {
                TerminateProcess(process_handle, 0);
                WaitForSingleObject(process_handle, 2000);
                if (GetExitCodeProcess(process_handle, &exit_code) && exit_code != STILL_ACTIVE)
                    last_exit_code_ = static_cast<int>(exit_code);
            }
            else
            {
                last_exit_code_ = static_cast<int>(exit_code);
            }
        }
        CloseHandle(process_handle);
    }
    if (proc_info_.hThread)
    {
        CloseHandle(proc_info_.hThread);
        proc_info_.hThread = nullptr;
    }
    if (job_)
    {
        CloseHandle(job_);
        job_ = nullptr;
    }
    attribute_storage_.clear();

    std::scoped_lock lock(output_mutex_);
    output_chunks_.clear();
}

void ConPtyProcess::request_close()
{
    PERF_MEASURE();
    if (input_write_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(input_write_);
        input_write_ = INVALID_HANDLE_VALUE;
    }
}

bool ConPtyProcess::is_running() const
{
    if (!proc_info_.hProcess)
        return false;
    DWORD exit_code = 0;
    GetExitCodeProcess(proc_info_.hProcess, &exit_code);
    if (exit_code != STILL_ACTIVE)
        last_exit_code_ = static_cast<int>(exit_code);
    return exit_code == STILL_ACTIVE;
}

std::optional<int> ConPtyProcess::exit_code() const
{
    if (is_running())
        return std::nullopt;
    return last_exit_code_;
}

std::string ConPtyProcess::current_working_directory() const
{
    if (!is_running() || !proc_info_.hProcess)
        return {};
    return read_remote_current_directory(proc_info_.hProcess);
}

std::optional<AgentProcessObservation>
ConPtyProcess::foreground_process_observation() const
{
    if (!is_running() || proc_info_.dwProcessId == 0)
        return std::nullopt;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return std::nullopt;

    struct ProcessEntry
    {
        DWORD process_id = 0;
        DWORD parent_process_id = 0;
    };
    std::vector<ProcessEntry> entries;
    std::unordered_map<DWORD, DWORD> parents;
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            entries.push_back({ entry.th32ProcessID, entry.th32ParentProcessID });
            parents[entry.th32ProcessID] = entry.th32ParentProcessID;
        } while (entries.size() < 4096 && Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    const DWORD root = proc_info_.dwProcessId;
    const auto belongs_to_tree = [&](DWORD process_id) {
        for (size_t depth = 0; depth < 64 && process_id != 0; ++depth)
        {
            if (process_id == root)
                return true;
            const auto parent = parents.find(process_id);
            if (parent == parents.end() || parent->second == process_id)
                break;
            process_id = parent->second;
        }
        return false;
    };

    AgentProcessObservation observation;
    observation.captured_at = std::chrono::steady_clock::now();
    // ConPTY does not expose a portable foreground process-group query.
    // Descendant membership is useful but remains explicitly fallible evidence.
    observation.foreground_reliable = false;
    for (const ProcessEntry& process : entries)
    {
        if (!belongs_to_tree(process.process_id))
            continue;
        std::string executable = process_executable_name(process.process_id);
        if (executable.empty())
            continue;
        std::vector<std::string> arguments;
        std::string hint;
        read_process_arguments_and_hint(
            process.process_id, &arguments, &hint);
        observation.processes.push_back({
            .process_id = process.process_id,
            .parent_process_id = process.parent_process_id,
            .executable = std::move(executable),
            .arguments = std::move(arguments),
            .agent_hint = std::move(hint),
        });
        if (observation.processes.size() >= 128)
            break;
    }
    return observation;
}

bool ConPtyProcess::resize(int cols, int rows)
{
    PERF_MEASURE();
    if (!pty_)
        return false;
    COORD size = {
        static_cast<SHORT>(std::clamp(cols, 1, 320)),
        static_cast<SHORT>(std::clamp(rows, 1, 200)),
    };
    return SUCCEEDED(ResizePseudoConsole(pty_, size));
}

bool ConPtyProcess::write(std::string_view text)
{
    PERF_MEASURE();
    if (input_write_ == INVALID_HANDLE_VALUE)
        return false;

    size_t total_written = 0;
    while (total_written < text.size())
    {
        DWORD written = 0;
        const DWORD to_write = static_cast<DWORD>(
            std::min<size_t>(text.size() - total_written, MAXDWORD));
        if (!WriteFile(input_write_, text.data() + total_written, to_write, &written, nullptr))
            return false;
        if (written == 0)
            return false;
        total_written += written;
    }
    return true;
}

std::vector<std::string> ConPtyProcess::drain_output()
{
    PERF_MEASURE();
    std::scoped_lock lock(output_mutex_);
    std::vector<std::string> drained;
    drained.swap(output_chunks_);
    return drained;
}

void ConPtyProcess::reader_main()
{
    PERF_MEASURE();
    char buffer[4096];
    while (reader_running_)
    {
        DWORD bytes_read = 0;
        if (!ReadFile(output_read_, buffer, sizeof(buffer), &bytes_read, nullptr) || bytes_read == 0)
            break;

        {
            std::scoped_lock lock(output_mutex_);
            output_chunks_.emplace_back(buffer, buffer + bytes_read);
        }

        if (on_output_available_)
            on_output_available_();
    }
}

} // namespace draxul
