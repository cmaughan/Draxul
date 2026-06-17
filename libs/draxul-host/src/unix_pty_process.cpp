#include "unix_pty_process.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <draxul/perf_timing.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

extern char** environ;

namespace draxul
{

namespace
{

bool starts_with(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::vector<std::string> build_child_environment()
{
    std::vector<std::string> env;
    for (char** current = environ; current != nullptr && *current != nullptr; ++current)
    {
        const std::string_view entry(*current);
        if (starts_with(entry, "TERM=")
            || starts_with(entry, "COLORTERM=")
            || starts_with(entry, "TERM_PROGRAM="))
        {
            continue;
        }
        env.emplace_back(entry);
    }

    env.emplace_back("TERM=xterm-256color");
    env.emplace_back("COLORTERM=truecolor");
    env.emplace_back("TERM_PROGRAM=draxul");
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

UnixPtyProcess::~UnixPtyProcess()
{
    shutdown();
}

bool UnixPtyProcess::spawn(const std::string& command, const std::vector<std::string>& args,
    const std::string& working_dir, std::function<void()> on_output_available,
    int initial_cols, int initial_rows)
{
    PERF_MEASURE();
    shutdown();
    last_exit_code_.reset();

    // Suppress SIGPIPE so writes to a closed PTY master return EPIPE instead
    // of delivering a fatal signal. Safe for a GUI application.
    signal(SIGPIPE, SIG_IGN);

    // Create a self-pipe so the reader thread can be woken on shutdown.
    if (pipe(shutdown_pipe_) < 0)
        return false;
    fcntl(shutdown_pipe_[0], F_SETFD, FD_CLOEXEC);
    fcntl(shutdown_pipe_[1], F_SETFD, FD_CLOEXEC);

    struct winsize ws = {};
    ws.ws_col = static_cast<unsigned short>(std::clamp(initial_cols, 1, 320));
    ws.ws_row = static_cast<unsigned short>(std::clamp(initial_rows, 1, 200));
    std::vector<std::string> child_env = build_child_environment();
    std::vector<std::string> exec_paths = resolve_exec_paths(command);
    std::string login_argv0 = "-";
    const auto slash = command.rfind('/');
    login_argv0 += (slash == std::string::npos) ? command : command.substr(slash + 1);

    std::vector<char*> child_argv;
    child_argv.reserve(args.size() + 2);
    child_argv.push_back(login_argv0.data());
    for (const auto& arg : args)
        child_argv.push_back(const_cast<char*>(arg.c_str()));
    child_argv.push_back(nullptr);

    std::vector<char*> child_envp;
    child_envp.reserve(child_env.size() + 1);
    for (auto& entry : child_env)
        child_envp.push_back(entry.data());
    child_envp.push_back(nullptr);

    std::vector<const char*> exec_path_ptrs;
    exec_path_ptrs.reserve(exec_paths.size());
    for (const auto& path : exec_paths)
        exec_path_ptrs.push_back(path.c_str());

    int slave_fd = -1;
    if (openpty(&master_fd_, &slave_fd, nullptr, nullptr, &ws) < 0)
    {
        close(shutdown_pipe_[0]);
        close(shutdown_pipe_[1]);
        shutdown_pipe_[0] = shutdown_pipe_[1] = -1;
        return false;
    }

    pid_ = fork();
    if (pid_ < 0)
    {
        close(slave_fd);
        close(master_fd_);
        master_fd_ = -1;
        close(shutdown_pipe_[0]);
        close(shutdown_pipe_[1]);
        shutdown_pipe_[0] = shutdown_pipe_[1] = -1;
        return false;
    }

    if (pid_ == 0)
    {
        // Child process: become session leader, attach PTY, exec the shell.
        close(master_fd_);
        close(shutdown_pipe_[0]);
        close(shutdown_pipe_[1]);
        setsid();

        if (ioctl(slave_fd, TIOCSCTTY, 0) < 0)
            _exit(127);

        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);
        if (slave_fd > STDERR_FILENO)
            close(slave_fd);

        // Close all inherited file descriptors above stderr to prevent FD
        // leakage into the child (log files, SDL/GPU FDs, other hosts' pipe
        // ends). The child only needs stdin/stdout/stderr which are already
        // set up via dup2 above.
        {
            const int max_fd = static_cast<int>(sysconf(_SC_OPEN_MAX));
            const int limit = (max_fd > 0) ? max_fd : 1024;
            for (int fd = STDERR_FILENO + 1; fd < limit; ++fd)
                close(fd); // harmless if fd is not open
        }

        // Restore SIGPIPE to default so child processes (and their pipelines)
        // terminate correctly. The parent set SIG_IGN before fork().
        signal(SIGPIPE, SIG_DFL);

        if (!working_dir.empty() && chdir(working_dir.c_str()) != 0)
            _exit(127);

        int exec_errno = ENOENT;
        for (const char* path : exec_path_ptrs)
        {
            execve(path,
                child_argv.data(),
                child_envp.data());
            if (errno != ENOENT && errno != ENOTDIR)
                exec_errno = errno;
        }
        errno = exec_errno;
        _exit(127);
    }

    // Parent process.
    close(slave_fd);
    on_output_available_ = std::move(on_output_available);
    reader_running_ = true;
    reader_thread_ = std::thread([this]() { reader_main(); });
    return true;
}

void UnixPtyProcess::shutdown()
{
    PERF_MEASURE();
    reader_running_ = false;

    // Signal the reader thread to wake up immediately via the shutdown pipe.
    if (shutdown_pipe_[1] >= 0)
    {
        (void)::write(shutdown_pipe_[1], "x", 1);
    }

    // Capture all state that the background reaper thread needs. Join the
    // reader synchronously before member fds/callbacks can be reused or
    // destroyed; the shutdown pipe makes this return promptly.
    // CLAUDE.md: "Keep shutdown paths non-blocking; a stuck Neovim child
    // must not hang the UI on exit."
    const pid_t pid_copy = pid_;
    const pid_t fg_pgid = (pid_copy > 0 && master_fd_ >= 0) ? tcgetpgrp(master_fd_) : -1;
    const int master_fd_copy = master_fd_;
    const int pipe0_copy = shutdown_pipe_[0];
    const int pipe1_copy = shutdown_pipe_[1];
    std::thread reader_copy = std::move(reader_thread_);

    if (reader_copy.joinable())
        reader_copy.join();

    pid_ = -1;
    master_fd_ = -1;
    shutdown_pipe_[0] = -1;
    shutdown_pipe_[1] = -1;

    if (pid_copy > 0)
    {
        // Phase 1: SIGTERM both groups + the direct child (non-blocking).
        kill(pid_copy, SIGTERM);
        kill(-pid_copy, SIGTERM);
        if (fg_pgid > 0 && fg_pgid != pid_copy)
            kill(-fg_pgid, SIGTERM);

        // Offload the timed wait + SIGKILL escalation + fd cleanup to a
        // detached background thread.
        std::thread(
            [pid_copy, fg_pgid, master_fd_copy, pipe0_copy, pipe1_copy]() {
                // Grace period: wait up to ~100ms for the direct child to exit.
                bool child_reaped = false;
                int status = 0;
                for (int i = 0; i < 10; ++i)
                {
                    if (const pid_t ret = waitpid(pid_copy, &status, WNOHANG);
                        ret == pid_copy || (ret < 0 && errno == ECHILD))
                    {
                        child_reaped = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(10000));
                }

                // Phase 2: SIGKILL anything still alive. Always kill the foreground
                // group regardless of whether the shell has already exited — the
                // foreground program (btop, nvim, etc.) may still be running.
                if (fg_pgid > 0 && fg_pgid != pid_copy)
                    kill(-fg_pgid, SIGKILL);
                if (!child_reaped)
                {
                    kill(pid_copy, SIGKILL);
                    kill(-pid_copy, SIGKILL);

                    // Non-blocking reap with bounded timeout.
                    for (int i = 0; i < 50; ++i)
                    {
                        if (const pid_t ret = waitpid(pid_copy, &status, WNOHANG);
                            ret == pid_copy || (ret < 0 && errno == ECHILD))
                            break;
                        std::this_thread::sleep_for(std::chrono::microseconds(10000));
                    }
                }

                if (master_fd_copy >= 0)
                    close(master_fd_copy);
                if (pipe0_copy >= 0)
                    close(pipe0_copy);
                if (pipe1_copy >= 0)
                    close(pipe1_copy);
            })
            .detach();
    }
    else
    {
        // No child process — clean up fds synchronously.
        if (master_fd_copy >= 0)
            close(master_fd_copy);
        if (pipe0_copy >= 0)
            close(pipe0_copy);
        if (pipe1_copy >= 0)
            close(pipe1_copy);
    }

    std::scoped_lock lock(output_mutex_);
    output_chunks_.clear();
}

void UnixPtyProcess::request_close()
{
    PERF_MEASURE();
    reader_running_ = false;

    // Signal the reader thread via the shutdown pipe. Do NOT close master_fd_
    // here — the reader thread may still be polling it. shutdown() will close
    // fds after joining the reader thread.
    if (shutdown_pipe_[1] >= 0)
        (void)::write(shutdown_pipe_[1], "x", 1);
}

bool UnixPtyProcess::is_running() const
{
    update_exit_status();
    return pid_ > 0;
}

std::optional<int> UnixPtyProcess::exit_code() const
{
    update_exit_status();
    return last_exit_code_;
}

bool UnixPtyProcess::resize(int cols, int rows) const
{
    PERF_MEASURE();
    if (master_fd_ < 0)
        return false;
    struct winsize ws = {};
    ws.ws_col = static_cast<unsigned short>(std::clamp(cols, 1, 320));
    ws.ws_row = static_cast<unsigned short>(std::clamp(rows, 1, 200));
    return ioctl(master_fd_, TIOCSWINSZ, &ws) == 0;
}

bool UnixPtyProcess::write(std::string_view text) const
{
    PERF_MEASURE();
    if (master_fd_ < 0)
        return false;
    const char* ptr = text.data();
    size_t remaining = text.size();
    while (remaining > 0)
    {
        const ssize_t written = ::write(master_fd_, ptr, remaining);
        if (written < 0)
        {
            if (errno == EINTR)
                continue;
            return false; // real write error
        }
        if (written == 0)
            return false; // unexpected: write to PTY returned 0
        ptr += written;
        remaining -= static_cast<size_t>(written);
    }
    return true;
}

std::vector<std::string> UnixPtyProcess::drain_output()
{
    PERF_MEASURE();
    std::scoped_lock lock(output_mutex_);
    std::vector<std::string> drained;
    drained.swap(output_chunks_);
    return drained;
}

void UnixPtyProcess::reader_main()
{
    PERF_MEASURE();
    std::array<char, 4096> buffer{};

    struct pollfd fds[2];
    fds[0].fd = master_fd_;
    fds[0].events = POLLIN;
    fds[1].fd = shutdown_pipe_[0];
    fds[1].events = POLLIN;

    while (reader_running_)
    {
        const int ret = poll(fds, 2, -1);
        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            break; // real error
        }
        if (ret == 0)
            continue; // timeout (shouldn't happen with -1 timeout)

        // Shutdown pipe signaled — exit immediately.
        if (fds[1].revents & POLLIN)
            break;

        // Master fd: drain readable data before acting on hangup.
        if (fds[0].revents & POLLIN)
        {
            const ssize_t bytes_read = ::read(master_fd_, buffer.data(), buffer.size());
            if (bytes_read <= 0)
                break;

            {
                std::scoped_lock lock(output_mutex_);
                output_chunks_.emplace_back(buffer.data(), buffer.data() + bytes_read);
            }

            if (on_output_available_)
                on_output_available_();
        }
        else if (fds[0].revents & (POLLHUP | POLLERR))
        {
            break;
        }
    }
}

void UnixPtyProcess::update_exit_status() const
{
    if (pid_ <= 0 || last_exit_code_.has_value())
        return;

    int status = 0;
    const pid_t result = waitpid(pid_, &status, WNOHANG);
    if (result == 0)
        return;
    if (result < 0)
    {
        if (errno == ECHILD)
            pid_ = -1;
        return;
    }

    if (WIFEXITED(status))
        last_exit_code_ = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        last_exit_code_ = 128 + WTERMSIG(status);
    pid_ = -1;
}

} // namespace draxul
