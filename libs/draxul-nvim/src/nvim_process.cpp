#include <draxul/log.h>
#include <draxul/nvim_rpc.h>
#include <draxul/perf_timing.h>

#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#endif

namespace draxul
{

struct NvimProcess::Impl
{
#ifdef _WIN32
    HANDLE child_stdin_write_ = INVALID_HANDLE_VALUE;
    HANDLE child_stdout_read_ = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION proc_info_ = {};
#else
    int child_stdin_write_ = -1;
    int child_stdout_read_ = -1;
    pid_t child_pid_ = -1;
#endif
    bool started_ = false;
};

NvimProcess::NvimProcess()
    : impl_(std::make_unique<Impl>())
{
}
NvimProcess::~NvimProcess() = default;

#ifdef _WIN32

namespace
{

std::string quote_windows_arg(const std::string& value)
{
    PERF_MEASURE();
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

} // namespace

bool NvimProcess::spawn(const std::string& nvim_path, const std::vector<std::string>& extra_args, const std::string& working_dir)
{
    PERF_MEASURE();
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE stdin_read, stdin_write, stdout_read, stdout_write;

    if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0))
    {
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to create stdin pipe");
        return false;
    }
    SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0))
    {
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to create stdout pipe");
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        return false;
    }
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);

    HANDLE nul_handle = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
        &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.hStdInput = stdin_read;
    si.hStdOutput = stdout_write;
    si.hStdError = nul_handle;
    si.dwFlags |= STARTF_USESTDHANDLES;

    std::ostringstream command;
    command << quote_windows_arg(nvim_path) << " --embed";
    for (const auto& arg : extra_args)
        command << ' ' << quote_windows_arg(arg);
    std::string cmd = command.str();

    const char* cwd = working_dir.empty() ? nullptr : working_dir.c_str();
    if (!CreateProcessA(
            nullptr,
            cmd.data(),
            nullptr, nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr, cwd,
            &si, &impl_->proc_info_))
    {
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to spawn nvim: error %lu", GetLastError());
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        if (nul_handle != INVALID_HANDLE_VALUE)
            CloseHandle(nul_handle);
        return false;
    }

    CloseHandle(stdin_read);
    CloseHandle(stdout_write);
    if (nul_handle != INVALID_HANDLE_VALUE)
        CloseHandle(nul_handle);

    impl_->child_stdin_write_ = stdin_write;
    impl_->child_stdout_read_ = stdout_read;
    impl_->started_ = true;

    DRAXUL_LOG_INFO(LogCategory::Nvim, "nvim spawned (PID %lu)", impl_->proc_info_.dwProcessId);
    return true;
}

void NvimProcess::shutdown()
{
    PERF_MEASURE();
    if (!impl_->started_)
        return;

    if (impl_->child_stdin_write_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(impl_->child_stdin_write_);
        impl_->child_stdin_write_ = INVALID_HANDLE_VALUE;
    }
    if (impl_->child_stdout_read_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(impl_->child_stdout_read_);
        impl_->child_stdout_read_ = INVALID_HANDLE_VALUE;
    }

    if (impl_->proc_info_.hProcess)
    {
        WaitForSingleObject(impl_->proc_info_.hProcess, 2000);
        TerminateProcess(impl_->proc_info_.hProcess, 0);
        CloseHandle(impl_->proc_info_.hProcess);
        CloseHandle(impl_->proc_info_.hThread);
    }

    impl_->started_ = false;
}

bool NvimProcess::write(const uint8_t* data, size_t len) const
{
    DWORD written;
    return WriteFile(impl_->child_stdin_write_, data, (DWORD)len, &written, nullptr) && written == len;
}

int NvimProcess::read(uint8_t* buffer, size_t max_len) const
{
    DWORD bytes_read;
    if (!ReadFile(impl_->child_stdout_read_, buffer, (DWORD)max_len, &bytes_read, nullptr))
    {
        return -1;
    }
    return (int)bytes_read;
}

bool NvimProcess::is_running() const
{
    if (!impl_->started_)
        return false;
    DWORD exit_code;
    GetExitCodeProcess(impl_->proc_info_.hProcess, &exit_code);
    return exit_code == STILL_ACTIVE;
}

#else // POSIX (macOS, Linux)

bool NvimProcess::spawn(const std::string& nvim_path, const std::vector<std::string>& extra_args, const std::string& working_dir)
{
    PERF_MEASURE();
    std::array<int, 2> stdin_pipe;
    std::array<int, 2> stdout_pipe;
    std::array<int, 2> exec_status_pipe;

    if (pipe(stdin_pipe.data()) != 0)
    {
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to create stdin pipe: %s", strerror(errno));
        return false;
    }
    if (pipe(stdout_pipe.data()) != 0)
    {
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to create stdout pipe: %s", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return false;
    }
    if (pipe(exec_status_pipe.data()) != 0)
    {
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to create exec-status pipe: %s", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return false;
    }

    if (fcntl(exec_status_pipe[1], F_SETFD, FD_CLOEXEC) != 0)
    {
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to configure exec-status pipe: %s", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(exec_status_pipe[0]);
        close(exec_status_pipe[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to fork: %s", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(exec_status_pipe[0]);
        close(exec_status_pipe[1]);
        return false;
    }

    if (pid == 0)
    {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(exec_status_pipe[0]);

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0)
        {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        if (!working_dir.empty() && chdir(working_dir.c_str()) != 0)
        {
            int chdir_errno = errno;
            (void)!::write(exec_status_pipe[1], &chdir_errno, sizeof(chdir_errno));
            close(exec_status_pipe[1]);
            _exit(127);
        }

        std::vector<std::string> argv_storage;
        argv_storage.reserve(extra_args.size() + 2);
        argv_storage.push_back(nvim_path);
        argv_storage.emplace_back("--embed");
        for (const auto& arg : extra_args)
            argv_storage.push_back(arg);

        std::vector<char*> argv;
        argv.reserve(argv_storage.size() + 1);
        for (auto& arg : argv_storage)
            argv.push_back(arg.data());
        argv.push_back(nullptr);

        execvp(nvim_path.c_str(), argv.data());
        int exec_errno = errno;
        (void)!::write(exec_status_pipe[1], &exec_errno, sizeof(exec_errno));
        close(exec_status_pipe[1]);
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(exec_status_pipe[1]);

    int exec_errno = 0;
    ssize_t status_bytes = ::read(exec_status_pipe[0], &exec_errno, sizeof(exec_errno));
    close(exec_status_pipe[0]);
    if (status_bytes > 0)
    {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        int status = 0;
        waitpid(pid, &status, 0);
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to spawn nvim: %s", strerror(exec_errno));
        return false;
    }

    impl_->child_stdin_write_ = stdin_pipe[1];
    impl_->child_stdout_read_ = stdout_pipe[0];
    impl_->child_pid_ = pid;
    impl_->started_ = true;

    DRAXUL_LOG_INFO(LogCategory::Nvim, "nvim spawned (PID %d)", (int)impl_->child_pid_);
    return true;
}

void NvimProcess::shutdown()
{
    PERF_MEASURE();
    if (!impl_->started_)
        return;

    if (impl_->child_stdin_write_ >= 0)
    {
        close(impl_->child_stdin_write_);
        impl_->child_stdin_write_ = -1;
    }
    if (impl_->child_stdout_read_ >= 0)
    {
        close(impl_->child_stdout_read_);
        impl_->child_stdout_read_ = -1;
    }

    if (impl_->child_pid_ > 0)
    {
        int status;
        pid_t result = waitpid(impl_->child_pid_, &status, WNOHANG);
        if (result == 0)
        {
            kill(impl_->child_pid_, SIGTERM);
            std::this_thread::sleep_for(std::chrono::microseconds(500000));
            result = waitpid(impl_->child_pid_, &status, WNOHANG);
            if (result == 0)
            {
                kill(impl_->child_pid_, SIGKILL);
                waitpid(impl_->child_pid_, &status, 0);
            }
        }
        impl_->child_pid_ = -1;
    }

    impl_->started_ = false;
}

bool NvimProcess::write(const uint8_t* data, size_t len) const
{
    size_t total_written = 0;
    while (total_written < len)
    {
        ssize_t n = ::write(impl_->child_stdin_write_, data + total_written, len - total_written);
        if (n <= 0)
            return false;
        total_written += (size_t)n;
    }
    return true;
}

int NvimProcess::read(uint8_t* buffer, size_t max_len) const
{
    ssize_t n = ::read(impl_->child_stdout_read_, buffer, max_len);
    if (n < 0)
        return -1;
    return (int)n;
}

bool NvimProcess::is_running() const
{
    if (!impl_->started_ || impl_->child_pid_ <= 0)
        return false;
    int status;
    pid_t result = waitpid(impl_->child_pid_, &status, WNOHANG);
    return result == 0;
}

#endif

} // namespace draxul
