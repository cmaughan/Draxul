#include "conpty_process.h"

#include <algorithm>
#include <atomic>
#include <draxul/perf_timing.h>
#include <mutex>
#include <thread>

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
    quoted.push_back('"');
    return quoted;
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

    STARTUPINFOEXA startup = {};
    startup.StartupInfo.cb = sizeof(startup);
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

    const char* cwd = working_dir.empty() ? nullptr : working_dir.c_str();
    const bool created = CreateProcessA(nullptr, command_line.data(), nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT, nullptr, cwd, &startup.StartupInfo, &proc_info_);

    DeleteProcThreadAttributeList(startup.lpAttributeList);
    attribute_storage_.clear();
    CloseHandle(pty_input_read);
    CloseHandle(pty_output_write);

    if (!created)
    {
        cleanup();
        return false;
    }

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
        WaitForSingleObject(proc_info_.hProcess, 1000);
        DWORD exit_code = STILL_ACTIVE;
        GetExitCodeProcess(proc_info_.hProcess, &exit_code);
        if (exit_code == STILL_ACTIVE)
            TerminateProcess(proc_info_.hProcess, 0);
        CloseHandle(proc_info_.hProcess);
        proc_info_.hProcess = nullptr;
    }
    if (proc_info_.hThread)
    {
        CloseHandle(proc_info_.hThread);
        proc_info_.hThread = nullptr;
    }

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
    return exit_code == STILL_ACTIVE;
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
