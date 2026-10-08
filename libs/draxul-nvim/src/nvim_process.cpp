#include <draxul/log.h>
#include <draxul/nvim_transport.h>
#include <draxul/perf_timing.h>
#include <draxul/process_util.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <limits>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
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
    // Self-pipe that wakes a writer polling the non-blocking stdin pipe.
    // Created by spawn() and closed only by the destructor, after every
    // writer has left write().
    int cancel_read_ = -1;
    int cancel_write_ = -1;
#endif
    std::atomic<bool> started_{ false };

    // Write cancellation. writer_mutex_ guards the in-progress writer
    // bookkeeping so cancel_writes() can wait (bounded) for writers to leave
    // the pipe before shutdown closes it. On Windows the registered thread ids
    // are the targets of CancelSynchronousIo; holding the mutex while
    // cancelling keeps each target inside write() and therefore alive.
    std::atomic<bool> write_cancelled_{ false };
    mutable std::mutex writer_mutex_;
    mutable std::condition_variable writers_idle_cv_;
#ifdef _WIN32
    std::vector<DWORD> writer_threads_;
#else
    int active_writers_ = 0;
#endif

    ~Impl()
    {
#ifndef _WIN32
        if (cancel_read_ >= 0)
            close(cancel_read_);
        if (cancel_write_ >= 0)
            close(cancel_write_);
#endif
    }
};

namespace
{
// Bounded wait for blocked writers to observe cancellation. Cancellation
// wakes them immediately; the bound only guards against a platform that
// fails to interrupt the write so shutdown can never hang on the child.
constexpr auto kWriterCancelWait = std::chrono::milliseconds(500);
} // namespace

NvimProcess::NvimProcess()
    : impl_(std::make_unique<Impl>())
{
}
NvimProcess::~NvimProcess() = default;

#ifdef _WIN32

namespace
{

bool ascii_iequals(std::wstring_view lhs, std::wstring_view rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (size_t i = 0; i < lhs.size(); ++i)
    {
        const wchar_t a = lhs[i];
        const wchar_t b = rhs[i];
        if ((a >= L'A' && a <= L'Z' ? a + (L'a' - L'A') : a)
            != (b >= L'A' && b <= L'Z' ? b + (L'a' - L'A') : b))
            return false;
    }
    return true;
}

bool widen_utf8(std::string_view text, std::wstring& wide)
{
    if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        return false;
    if (text.empty())
    {
        wide.clear();
        return true;
    }
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0)
        return false;
    wide.resize(static_cast<size_t>(count));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
               text.data(), static_cast<int>(text.size()), wide.data(), count)
        == count;
}

std::vector<wchar_t> build_windows_environment_block_with_term_dumb()
{
    LPWCH inherited_block = GetEnvironmentStringsW();
    if (!inherited_block)
        return { L'T', L'E', L'R', L'M', L'=', L'd', L'u', L'm', L'b', L'\0', L'\0' };

    std::vector<std::wstring> entries;
    bool term_overridden = false;
    for (const wchar_t* cursor = inherited_block; *cursor != L'\0'; cursor += std::wcslen(cursor) + 1)
    {
        std::wstring entry(cursor);
        if (!entry.empty() && entry[0] != L'=')
        {
            const size_t equals = entry.find(L'=');
            if (equals != std::wstring::npos
                && ascii_iequals(std::wstring_view(entry.data(), equals), L"TERM"))
            {
                entry = L"TERM=dumb";
                term_overridden = true;
            }
        }
        entries.push_back(std::move(entry));
    }
    FreeEnvironmentStringsW(inherited_block);

    if (!term_overridden)
        entries.emplace_back(L"TERM=dumb");

    size_t total_chars = 1; // trailing NUL
    for (const auto& entry : entries)
        total_chars += entry.size() + 1;

    std::vector<wchar_t> environment_block(total_chars, L'\0');
    wchar_t* out = environment_block.data();
    for (const auto& entry : entries)
    {
        std::memcpy(out, entry.data(), entry.size() * sizeof(wchar_t));
        out += entry.size();
        *out++ = L'\0';
    }
    *out = L'\0';
    return environment_block;
}

} // namespace

Result<void, Error> NvimProcess::spawn(const std::string& nvim_path, const std::vector<std::string>& extra_args, const std::string& working_dir)
{
    PERF_MEASURE();
    std::wstring wide_path;
    std::wstring wide_working_dir;
    if (!widen_utf8(nvim_path, wide_path) || !widen_utf8(working_dir, wide_working_dir))
        return Result<void, Error>::err(Error::spawn("Invalid UTF-8 in Neovim executable or working directory"));
    std::wstring command = quote_windows_arg(std::wstring_view(wide_path)) + L" --embed";
    for (const auto& arg : extra_args)
    {
        std::wstring wide_arg;
        if (!widen_utf8(arg, wide_arg))
            return Result<void, Error>::err(Error::spawn("Invalid UTF-8 in Neovim argument"));
        command += L' ';
        command += quote_windows_arg(std::wstring_view(wide_arg));
    }
    std::vector<wchar_t> env_block = build_windows_environment_block_with_term_dumb();

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

    HANDLE nul_handle = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
        &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.hStdInput = stdin_read;
    si.hStdOutput = stdout_write;
    si.hStdError = nul_handle;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION proc_info = {};
    if (!CreateProcessW(
            nullptr,
            command.data(),
            nullptr, nullptr,
            TRUE,
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
            env_block.data(), wide_working_dir.empty() ? nullptr : wide_working_dir.c_str(),
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

    impl_->write_cancelled_.store(false, std::memory_order_release);
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

    // A writer blocked on a child that stopped reading must leave WriteFile
    // before the handle is closed underneath it.
    cancel_writes();

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
        // The UI-facing owner relinquishes the handle immediately. A
        // self-contained reaper owns the bounded wait, escalation, and close.
        std::thread([process_handle] {
            const DWORD wait_result = WaitForSingleObject(process_handle, 2000);
            if (wait_result == WAIT_TIMEOUT)
            {
                TerminateProcess(process_handle, 0);
                WaitForSingleObject(process_handle, 2000);
            }
            CloseHandle(process_handle);
        }).detach();
    }
}

bool NvimProcess::write(const uint8_t* data, size_t len) const
{
    HANDLE h = impl_->child_stdin_write_.load(std::memory_order_acquire);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    // Register this thread as a CancelSynchronousIo target for the duration
    // of the write. Anonymous pipes do not support overlapped I/O, so a
    // synchronous WriteFile blocked on a full pipe is cancelled per thread.
    const DWORD thread_id = GetCurrentThreadId();
    {
        std::lock_guard<std::mutex> lock(impl_->writer_mutex_);
        if (impl_->write_cancelled_.load(std::memory_order_acquire))
            return false;
        impl_->writer_threads_.push_back(thread_id);
    }
    struct WriterRegistration
    {
        Impl& impl;
        DWORD id;
        ~WriterRegistration()
        {
            std::lock_guard<std::mutex> lock(impl.writer_mutex_);
            auto it = std::find(impl.writer_threads_.begin(), impl.writer_threads_.end(), id);
            if (it != impl.writer_threads_.end())
                impl.writer_threads_.erase(it);
            impl.writers_idle_cv_.notify_all();
        }
    } registration{ *impl_, thread_id };

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
    //  * cancel_writes() -> WriteFile fails with ERROR_OPERATION_ABORTED, or
    //    the flag is observed before the next chunk.
    size_t total_written = 0;
    while (total_written < len)
    {
        if (impl_->write_cancelled_.load(std::memory_order_acquire))
            return false;
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

void NvimProcess::cancel_writes() const
{
    impl_->write_cancelled_.store(true, std::memory_order_release);
    // A writer may register and pass its flag check just before entering
    // WriteFile, where a single CancelSynchronousIo would find no I/O to
    // cancel. Repeat until every registered writer has left, within a bound.
    const auto deadline = std::chrono::steady_clock::now() + kWriterCancelWait;
    std::unique_lock<std::mutex> lock(impl_->writer_mutex_);
    while (!impl_->writer_threads_.empty())
    {
        for (DWORD id : impl_->writer_threads_)
        {
            // The writer cannot deregister (and so cannot exit) while this
            // mutex is held, so the id still names the writing thread.
            if (HANDLE thread = OpenThread(THREAD_TERMINATE, FALSE, id))
            {
                CancelSynchronousIo(thread);
                CloseHandle(thread);
            }
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            DRAXUL_LOG_WARN(LogCategory::Nvim,
                "Timed out waiting for %zu nvim pipe writer(s) to cancel",
                impl_->writer_threads_.size());
            return;
        }
        impl_->writers_idle_cv_.wait_for(lock, std::chrono::milliseconds(1));
    }
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

// Blocking SIGPIPE only on the writing thread avoids changing the application's
// signal handlers. Consume a signal produced by our failed write before restoring
// the mask, but preserve any signal that was pending before the operation.
// Linux directs a pipe-write SIGPIPE at the writing thread, so this is sufficient
// there; macOS directs it at the process and relies on F_SETNOSIGPIPE in spawn().
class ScopedPipeSignalBlock
{
public:
    ScopedPipeSignalBlock()
    {
        sigemptyset(&pipe_signal_);
        sigaddset(&pipe_signal_, SIGPIPE);
        active_ = pthread_sigmask(SIG_BLOCK, &pipe_signal_, &previous_mask_) == 0;
        if (active_)
        {
            sigset_t pending;
            if (sigpending(&pending) == 0)
                was_pending_ = sigismember(&pending, SIGPIPE) == 1;
            else
            {
                pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr);
                active_ = false;
            }
        }
    }

    ~ScopedPipeSignalBlock()
    {
        if (active_)
            pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr);
    }

    bool active() const { return active_; }

    void consume_write_failure() const
    {
        if (was_pending_)
            return;
        sigset_t pending;
        if (sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1)
        {
            // SIGPIPE from write is thread-directed and already pending, so
            // sigwait cannot block here. Unlike sigtimedwait, it is on macOS.
            int signal_number = 0;
            sigwait(&pipe_signal_, &signal_number);
        }
    }

private:
    sigset_t pipe_signal_{};
    sigset_t previous_mask_{};
    bool active_ = false;
    bool was_pending_ = false;
};

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

#ifdef F_SETNOSIGPIPE
    // XNU raises SIGPIPE for a failed pipe write on the process, not the
    // writing thread, so the thread mask in write() cannot stop another thread
    // with default policy from taking the fatal signal. Mark only the parent's
    // write end; the child closes it and keeps default handling on its own pipes.
    if (fcntl(stdin_pipe[1], F_SETNOSIGPIPE, 1) != 0)
    {
        const int e = errno;
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to suppress SIGPIPE on stdin pipe: %s", strerror(e));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(exec_status_pipe[0]);
        close(exec_status_pipe[1]);
        return Result<void, Error>::err(Error::io(
            std::string("Failed to suppress SIGPIPE on stdin pipe: ") + strerror(e)));
    }
#endif

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

    // nvim --embed does not use termcap; TERM=dumb keeps it from probing.
    std::vector<std::string> child_env = build_child_environment("dumb");
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
        sigset_t pipe_signal;
        sigemptyset(&pipe_signal);
        sigaddset(&pipe_signal, SIGPIPE);
        sigprocmask(SIG_UNBLOCK, &pipe_signal, nullptr);

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

    // The parent's stdin end is non-blocking so write() can wait in poll()
    // alongside the cancellation self-pipe instead of blocking in the kernel
    // on a child that stopped reading. O_NONBLOCK is an open-file-description
    // flag; the child's read end is a separate description and is unaffected.
    if (const int flags = fcntl(stdin_pipe[1], F_GETFL); flags < 0
        || fcntl(stdin_pipe[1], F_SETFL, flags | O_NONBLOCK) != 0)
    {
        const int e = errno;
        DRAXUL_LOG_WARN(LogCategory::Nvim, "Failed to make nvim stdin non-blocking: %s", strerror(e));
    }

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

    // Re-arm write cancellation. The self-pipe outlives each child so a
    // writer never polls a descriptor that shutdown may have recycled.
    if (impl_->cancel_read_ < 0)
    {
        std::array<int, 2> cancel_pipe{ -1, -1 };
        if (pipe(cancel_pipe.data()) == 0)
        {
            for (int fd : cancel_pipe)
            {
                fcntl(fd, F_SETFD, FD_CLOEXEC);
                if (const int flags = fcntl(fd, F_GETFL); flags >= 0)
                    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
            }
            impl_->cancel_read_ = cancel_pipe[0];
            impl_->cancel_write_ = cancel_pipe[1];
        }
        else
        {
            const int e = errno;
            DRAXUL_LOG_WARN(LogCategory::Nvim,
                "Failed to create nvim write-cancel pipe (%s); writes poll for cancellation",
                strerror(e));
        }
    }
    else
    {
        std::array<char, 16> drain{};
        while (::read(impl_->cancel_read_, drain.data(), drain.size()) > 0)
        {
        }
    }
    impl_->write_cancelled_.store(false, std::memory_order_release);

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

    // Wake any writer waiting on a child that stopped reading and wait
    // (bounded) for it to leave write(), so the stdin descriptor is not
    // closed, and possibly reused, underneath it.
    cancel_writes();

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

    {
        std::lock_guard<std::mutex> lock(impl_->writer_mutex_);
        if (impl_->write_cancelled_.load(std::memory_order_acquire))
            return false;
        ++impl_->active_writers_;
    }
    struct WriterRegistration
    {
        Impl& impl;
        ~WriterRegistration()
        {
            std::lock_guard<std::mutex> lock(impl.writer_mutex_);
            --impl.active_writers_;
            impl.writers_idle_cv_.notify_all();
        }
    } registration{ *impl_ };

    ScopedPipeSignalBlock signal_block;
    if (!signal_block.active())
        return false;
    const int cancel_fd = impl_->cancel_read_;
    size_t total_written = 0;
    while (total_written < len)
    {
        if (impl_->write_cancelled_.load(std::memory_order_acquire))
            return false;
        ssize_t n = ::write(fd, data + total_written, len - total_written);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // The child is not reading. Wait for pipe space or for
                // cancel_writes(); without the self-pipe, poll in short
                // slices so the cancellation flag is still observed.
                std::array<pollfd, 2> fds{};
                fds[0].fd = fd;
                fds[0].events = POLLOUT;
                fds[1].fd = cancel_fd;
                fds[1].events = POLLIN;
                const nfds_t count = cancel_fd >= 0 ? 2 : 1;
                const int ready = poll(fds.data(), count, cancel_fd >= 0 ? -1 : 50);
                if (ready < 0 && errno != EINTR)
                    return false;
                if (count == 2 && fds[1].revents != 0)
                    return false;
                if ((fds[0].revents & POLLNVAL) != 0)
                    return false;
                // POLLOUT, POLLHUP, or POLLERR: retry; a closed reader
                // surfaces as EPIPE from the next write.
                continue;
            }
            if (errno == EPIPE)
                signal_block.consume_write_failure();
            return false;
        }
        if (n == 0)
            return false;
        total_written += (size_t)n;
    }
    return true;
}

void NvimProcess::cancel_writes() const
{
    {
        std::lock_guard<std::mutex> lock(impl_->writer_mutex_);
        impl_->write_cancelled_.store(true, std::memory_order_release);
    }
    if (impl_->cancel_write_ >= 0)
    {
        // Level-triggered and never drained until the next spawn(): one byte
        // wakes every current and future poll. A full pipe already wakes.
        const char byte = 1;
        ssize_t n;
        do
        {
            n = ::write(impl_->cancel_write_, &byte, 1);
        } while (n < 0 && errno == EINTR);
    }
    std::unique_lock<std::mutex> lock(impl_->writer_mutex_);
    if (!impl_->writers_idle_cv_.wait_for(lock, kWriterCancelWait, [&] { return impl_->active_writers_ == 0; }))
    {
        DRAXUL_LOG_WARN(LogCategory::Nvim,
            "Timed out waiting for %d nvim pipe writer(s) to cancel", impl_->active_writers_);
    }
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
