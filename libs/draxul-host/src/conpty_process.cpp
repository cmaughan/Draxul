#include "conpty_process.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace draxul
{

namespace
{

std::string quote_windows_arg(const std::string& value)
{
    if (value.find_first_of(" \t\"") == std::string::npos)
        return value;

    std::string quoted = "\"";
    size_t backslashes = 0;
    for (char ch : value)
    {
        if (ch == '\\')
        {
            ++backslashes;
            quoted.push_back(ch);
            continue;
        }
        if (ch == '"')
        {
            quoted.append(backslashes, '\\');
            quoted.push_back('\\');
        }
        backslashes = 0;
        quoted.push_back(ch);
    }
    quoted.append(backslashes, '\\');
    quoted.push_back('"');
    return quoted;
}

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

} // namespace

ConPtyProcess::~ConPtyProcess()
{
    shutdown();
}

bool ConPtyProcess::spawn(const std::string& command, const std::vector<std::string>& args,
    const std::string& working_dir, int initial_cols, int initial_rows,
    std::function<void()> on_output_available)
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
    const DWORD creation_flags = EXTENDED_STARTUPINFO_PRESENT;
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
        nullptr,
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
        // Offload the timed wait + TerminateProcess escalation to a detached
        // thread so we never block the main/UI thread on a stuck child.
        // CLAUDE.md: "Keep shutdown paths non-blocking; a stuck Neovim child
        // must not hang the UI on exit."
        HANDLE process_handle = proc_info_.hProcess;
        std::thread([process_handle]() {
            if (WaitForSingleObject(process_handle, 1000) == WAIT_TIMEOUT)
                TerminateProcess(process_handle, 0);
            CloseHandle(process_handle);
        }).detach();
        proc_info_.hProcess = nullptr;
    }
    if (proc_info_.hThread)
    {
        CloseHandle(proc_info_.hThread);
        proc_info_.hThread = nullptr;
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
    DWORD written = 0;
    return WriteFile(input_write_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr)
        && written == static_cast<DWORD>(text.size());
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
