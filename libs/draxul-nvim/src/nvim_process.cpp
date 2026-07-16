#include <draxul/log.h>
#include <draxul/nvim_rpc.h>
#include <draxul/perf_timing.h>
#include <draxul/process_util.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string_view>

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

#ifndef _WIN32
extern char** environ;
#endif

namespace draxul
{

struct NvimProcess::Impl
{
    // Pipe endpoints are accessed from the shutdown, reader, and RPC request
    // threads.  Their atomic exchange/load contract is unchanged (WI 134).
#ifdef _WIN32
    std::atomic<HANDLE> child_stdin_write_{ INVALID_HANDLE_VALUE };
    std::atomic<HANDLE> child_stdout_read_{ INVALID_HANDLE_VALUE };
    // The process handle is published and unpublished under this mutex.  A
    // caller may only use the handle while holding the mutex; shutdown takes
    // ownership by clearing the published handle before closing it.
    mutable std::mutex process_mutex_;
    HANDLE process_handle_ = nullptr;
#else
    std::atomic<int> child_stdin_write_{ -1 };
    std::atomic<int> child_stdout_read_{ -1 };
    std::atomic<pid_t> child_pid_{ -1 };
#endif
    std::atomic<bool> started_{ false };
};

NvimProcess::NvimProcess()
    : impl_(std::make_unique<Impl>())
{
}
NvimProcess::~NvimProcess() = default;

#ifdef _WIN32

namespace
{

bool ascii_iequals(std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (size_t i = 0; i < lhs.size(); ++i)
    {
        const unsigned char a = static_cast<unsigned char>(lhs[i]);
        const unsigned char b = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(a) != std::tolower(b))
            return false;
    }
    return true;
}

std::vector<char> build_windows_environment_block_with_term_dumb()
{
    LPCH inherited_block = GetEnvironmentStringsA();
    if (!inherited_block)
        return { 'T', 'E', 'R', 'M', '=', 'd', 'u', 'm', 'b', '\0', '\0' };

    std::vector<std::string> entries;
    bool term_overridden = false;
    for (const char* cursor = inherited_block; *cursor != '\0'; cursor += std::strlen(cursor) + 1)
    {
        std::string entry(cursor);
        if (!entry.empty() && entry[0] != '=')
        {
            const size_t equals = entry.find('=');
            if (equals != std::string::npos
                && ascii_iequals(std::string_view(entry.data(), equals), "TERM"))
            {
                entry = "TERM=dumb";
                term_overridden = true;
            }
        }
        entries.push_back(std::move(entry));
    }
    FreeEnvironmentStringsA(inherited_block);

    if (!term_overridden)
        entries.emplace_back("TERM=dumb");

    size_t total_bytes = 1; // trailing NUL
    for (const auto& entry : entries)
        total_bytes += entry.size() + 1;

    std::vector<char> environment_block(total_bytes, '\0');
    char* out = environment_block.data();
    for (const auto& entry : entries)
    {
        std::memcpy(out, entry.data(), entry.size());
        out += entry.size();
        *out++ = '\0';
    }
    *out = '\0';
    return environment_block;
}

} // namespace

Result<void, Error> NvimProcess::spawn(const std::string& nvim_path, const std::vector<std::string>& extra_args, const std::string& working_dir)
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
        return Result<void, Error>::err(Error::io("Failed to create stdin pipe"));
    }
    SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0))
    {
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to create stdout pipe");
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        return Result<void, Error>::err(Error::io("Failed to create stdout pipe"));
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
    std::vector<char> env_block = build_windows_environment_block_with_term_dumb();

    const char* cwd = working_dir.empty() ? nullptr : working_dir.c_str();
    PROCESS_INFORMATION proc_info = {};
    if (!CreateProcessA(
            nullptr,
            cmd.data(),
            nullptr, nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            env_block.empty() ? nullptr : env_block.data(), cwd,
            &si, &proc_info))
    {
        const DWORD err = GetLastError();
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to spawn nvim: error %lu", err);
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        if (nul_handle != INVALID_HANDLE_VALUE)
            CloseHandle(nul_handle);
        return Result<void, Error>::err(Error::spawn(
            "CreateProcess failed (GetLastError=" + std::to_string(err) + ")"));
    }

    CloseHandle(stdin_read);
    CloseHandle(stdout_write);
    if (nul_handle != INVALID_HANDLE_VALUE)
        CloseHandle(nul_handle);

    // The primary thread handle is not needed after CreateProcess returns.
    // Keeping only the process handle makes the ownership contract explicit.
    CloseHandle(proc_info.hThread);

    {
        std::lock_guard<std::mutex> lock(impl_->process_mutex_);
        impl_->child_stdin_write_.store(stdin_write, std::memory_order_relaxed);
        impl_->child_stdout_read_.store(stdout_read, std::memory_order_relaxed);
        impl_->process_handle_ = proc_info.hProcess;
        impl_->started_.store(true, std::memory_order_release);
    }

    DRAXUL_LOG_INFO(LogCategory::Nvim, "nvim spawned (PID %lu)", proc_info.dwProcessId);
    return Result<void, Error>::ok();
}

void NvimProcess::shutdown()
{
    PERF_MEASURE();

    HANDLE process_handle = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl_->process_mutex_);
        process_handle = impl_->process_handle_;
        impl_->process_handle_ = nullptr;
        impl_->started_.store(false, std::memory_order_release);
    }

    HANDLE stdin_h = impl_->child_stdin_write_.exchange(INVALID_HANDLE_VALUE, std::memory_order_acq_rel);
    if (stdin_h != INVALID_HANDLE_VALUE)
        CloseHandle(stdin_h);

    HANDLE stdout_h = impl_->child_stdout_read_.exchange(INVALID_HANDLE_VALUE, std::memory_order_acq_rel);
    if (stdout_h != INVALID_HANDLE_VALUE)
        CloseHandle(stdout_h);

    if (process_handle)
    {
        const DWORD wait_result = WaitForSingleObject(process_handle, 2000);
        if (wait_result == WAIT_TIMEOUT)
        {
            TerminateProcess(process_handle, 0);
            WaitForSingleObject(process_handle, 2000);
        }
        CloseHandle(process_handle);
    }
}

bool NvimProcess::write(const uint8_t* data, size_t len) const
{
    HANDLE h = impl_->child_stdin_write_.load(std::memory_order_acquire);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    // WriteFile on an anonymous/named pipe is allowed to perform a partial
    // write: it may return TRUE with `written` < `to_write` when the pipe's
    // kernel buffer is nearly full (e.g. large msgpack-RPC payloads such as
    // nvim_buf_set_lines with pasted content). A single call is therefore
    // not sufficient — we must loop until the whole buffer has been
    // delivered, otherwise the tail of the message is silently dropped and
    // the RPC stream becomes corrupt for every subsequent request.
    //
    // Failure cases:
    //  * WriteFile returns FALSE -> hard error (broken pipe, child exited).
    //  * WriteFile returns TRUE with written == 0 -> should not happen on a
    //    blocking handle, but we treat it as a hard error to avoid spinning
    //    forever if the kernel ever violates that contract.
    size_t total_written = 0;
    while (total_written < len)
    {
        DWORD written = 0;
        DWORD to_write = static_cast<DWORD>(
            std::min<size_t>(len - total_written, MAXDWORD));
        if (!WriteFile(h, data + total_written, to_write, &written, nullptr))
            return false;
        if (written == 0)
            return false;
        total_written += written;
    }
    return true;
}

int NvimProcess::read(uint8_t* buffer, size_t max_len) const
{
    HANDLE h = impl_->child_stdout_read_.load(std::memory_order_acquire);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    DWORD bytes_read;
    if (!ReadFile(h, buffer, (DWORD)max_len, &bytes_read, nullptr))
    {
        return -1;
    }
    return (bytes_read > static_cast<DWORD>(INT_MAX)) ? -1 : static_cast<int>(bytes_read);
}

bool NvimProcess::is_running() const
{
    std::lock_guard<std::mutex> lock(impl_->process_mutex_);
    if (!impl_->started_.load(std::memory_order_acquire) || !impl_->process_handle_)
        return false;
    DWORD exit_code = 0;
    return GetExitCodeProcess(impl_->process_handle_, &exit_code)
        && exit_code == STILL_ACTIVE;
}

#else // POSIX (macOS, Linux)

namespace
{

bool starts_with(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::vector<std::string> build_nvim_child_environment()
{
    std::vector<std::string> env;
    for (char** current = environ; current != nullptr && *current != nullptr; ++current)
    {
        const std::string_view entry(*current);
        if (starts_with(entry, "TERM="))
            continue;
        env.emplace_back(entry);
    }

    env.emplace_back("TERM=dumb");
    return env;
}

std::vector<std::string> resolve_exec_paths(const std::string& command)
{
    if (command.find('/') != std::string::npos)
        return { command };

    const char* raw_path = std::getenv("PATH");
    const std::string_view path
        = (raw_path != nullptr && *raw_path != '\0')
        ? std::string_view(raw_path)
        : std::string_view("/usr/bin:/bin:/usr/sbin:/sbin");

    std::vector<std::string> candidates;
    size_t start = 0;
    while (start <= path.size())
    {
        const size_t end = path.find(':', start);
        std::string_view dir = (end == std::string_view::npos)
            ? path.substr(start)
            : path.substr(start, end - start);
        if (dir.empty())
            dir = ".";

        std::string candidate(dir);
        if (!candidate.empty() && candidate.back() != '/')
            candidate.push_back('/');
        candidate += command;
        candidates.push_back(std::move(candidate));

        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }

    return candidates;
}

} // namespace

Result<void, Error> NvimProcess::spawn(const std::string& nvim_path, const std::vector<std::string>& extra_args, const std::string& working_dir)
{
    PERF_MEASURE();
    std::array<int, 2> stdin_pipe;
    std::array<int, 2> stdout_pipe;
    std::array<int, 2> exec_status_pipe;

    if (pipe(stdin_pipe.data()) != 0)
    {
        const int e = errno;
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to create stdin pipe: %s", strerror(e));
        return Result<void, Error>::err(Error::io(
            std::string("Failed to create stdin pipe: ") + strerror(e)));
    }
    if (pipe(stdout_pipe.data()) != 0)
    {
        const int e = errno;
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to create stdout pipe: %s", strerror(e));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return Result<void, Error>::err(Error::io(
            std::string("Failed to create stdout pipe: ") + strerror(e)));
    }
    if (pipe(exec_status_pipe.data()) != 0)
    {
        const int e = errno;
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to create exec-status pipe: %s", strerror(e));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return Result<void, Error>::err(Error::io(
            std::string("Failed to create exec-status pipe: ") + strerror(e)));
    }

    if (fcntl(exec_status_pipe[1], F_SETFD, FD_CLOEXEC) != 0)
    {
        const int e = errno;
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to configure exec-status pipe: %s", strerror(e));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(exec_status_pipe[0]);
        close(exec_status_pipe[1]);
        return Result<void, Error>::err(Error::io(
            std::string("Failed to configure exec-status pipe: ") + strerror(e)));
    }

    std::vector<std::string> argv_storage;
    argv_storage.reserve(extra_args.size() + 2);
    argv_storage.push_back(nvim_path);
    argv_storage.emplace_back("--embed");
    for (const auto& arg : extra_args)
        argv_storage.push_back(arg);

    std::vector<char*> child_argv;
    child_argv.reserve(argv_storage.size() + 1);
    for (auto& arg : argv_storage)
        child_argv.push_back(arg.data());
    child_argv.push_back(nullptr);

    std::vector<std::string> child_env = build_nvim_child_environment();
    std::vector<char*> child_envp;
    child_envp.reserve(child_env.size() + 1);
    for (auto& entry : child_env)
        child_envp.push_back(entry.data());
    child_envp.push_back(nullptr);

    std::vector<std::string> exec_paths = resolve_exec_paths(nvim_path);
    std::vector<const char*> exec_path_ptrs;
    exec_path_ptrs.reserve(exec_paths.size());
    for (const auto& path : exec_paths)
        exec_path_ptrs.push_back(path.c_str());

    pid_t pid = fork();
    if (pid < 0)
    {
        const int e = errno;
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to fork: %s", strerror(e));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(exec_status_pipe[0]);
        close(exec_status_pipe[1]);
        return Result<void, Error>::err(Error::spawn(
            std::string("fork() failed: ") + strerror(e)));
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

        // Close all inherited file descriptors above stderr to prevent FD
        // leakage into the child (log files, SDL/GPU FDs, other pipe ends).
        // exec_status_pipe[1] is kept open (it has FD_CLOEXEC so exec will
        // close it automatically, but we still need it for pre-exec error
        // reporting).
        {
            const int max_fd = static_cast<int>(sysconf(_SC_OPEN_MAX));
            const int limit = (max_fd > 0) ? max_fd : 1024;
            for (int fd = STDERR_FILENO + 1; fd < limit; ++fd)
            {
                if (fd == exec_status_pipe[1])
                    continue;
                close(fd); // harmless if fd is not open
            }
        }

        // Restore SIGPIPE to default so child processes terminate correctly
        // on broken pipes. The parent GUI may have set SIG_IGN.
        signal(SIGPIPE, SIG_DFL);

        if (!working_dir.empty() && chdir(working_dir.c_str()) != 0)
        {
            int chdir_errno = errno;
            (void)!::write(exec_status_pipe[1], &chdir_errno, sizeof(chdir_errno));
            close(exec_status_pipe[1]);
            _exit(127);
        }

        int exec_errno = ENOENT;
        for (const char* path : exec_path_ptrs)
        {
            execve(path, child_argv.data(), child_envp.data());
            if (errno != ENOENT && errno != ENOTDIR)
                exec_errno = errno;
        }
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
        return Result<void, Error>::err(Error::spawn(
            std::string("execvp(") + nvim_path + ") failed: " + strerror(exec_errno)));
    }

    impl_->child_stdin_write_.store(stdin_pipe[1], std::memory_order_relaxed);
    impl_->child_stdout_read_.store(stdout_pipe[0], std::memory_order_relaxed);
    impl_->child_pid_.store(pid, std::memory_order_relaxed);
    impl_->started_.store(true, std::memory_order_release);

    DRAXUL_LOG_INFO(LogCategory::Nvim, "nvim spawned (PID %d)", (int)impl_->child_pid_);
    return Result<void, Error>::ok();
}

void NvimProcess::shutdown()
{
    PERF_MEASURE();
    if (!impl_->started_.load(std::memory_order_acquire))
        return;

    // Atomically swap each fd to -1 so racing reader/writer threads see
    // the sentinel and fail their syscall cleanly instead of using a
    // closed fd.
    int stdin_fd = impl_->child_stdin_write_.exchange(-1, std::memory_order_acq_rel);
    if (stdin_fd >= 0)
        close(stdin_fd);

    int stdout_fd = impl_->child_stdout_read_.exchange(-1, std::memory_order_acq_rel);
    if (stdout_fd >= 0)
        close(stdout_fd);

    pid_t pid = impl_->child_pid_.exchange(-1, std::memory_order_acq_rel);
    if (pid > 0)
    {
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == 0)
        {
            // Send SIGTERM, then offload the timed wait + SIGKILL escalation
            // to a detached thread so the main thread does not block on a
            // stuck child. CLAUDE.md: "Keep shutdown paths non-blocking; a
            // stuck Neovim child must not hang the UI on exit."
            kill(pid, SIGTERM);
            std::thread([pid]() {
                using namespace std::chrono;
                const auto deadline = steady_clock::now() + milliseconds(500);
                int s = 0;
                while (steady_clock::now() < deadline)
                {
                    if (waitpid(pid, &s, WNOHANG) != 0)
                        return;
                    std::this_thread::sleep_for(milliseconds(20));
                }
                kill(pid, SIGKILL);
                waitpid(pid, &s, 0);
            }).detach();
        }
    }

    impl_->started_.store(false, std::memory_order_release);
}

bool NvimProcess::write(const uint8_t* data, size_t len) const
{
    int fd = impl_->child_stdin_write_.load(std::memory_order_acquire);
    if (fd < 0)
        return false;
    size_t total_written = 0;
    while (total_written < len)
    {
        ssize_t n = ::write(fd, data + total_written, len - total_written);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (n == 0)
            return false;
        total_written += (size_t)n;
    }
    return true;
}

int NvimProcess::read(uint8_t* buffer, size_t max_len) const
{
    int fd = impl_->child_stdout_read_.load(std::memory_order_acquire);
    if (fd < 0)
        return -1;
    ssize_t n;
    do
    {
        n = ::read(fd, buffer, max_len);
    } while (n < 0 && errno == EINTR);
    if (n < 0)
        return -1;
    return (int)n;
}

bool NvimProcess::is_running() const
{
    if (!impl_->started_.load(std::memory_order_acquire))
        return false;
    pid_t pid = impl_->child_pid_.load(std::memory_order_acquire);
    if (pid <= 0)
        return false;
    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);
    return result == 0;
}

#endif

} // namespace draxul
