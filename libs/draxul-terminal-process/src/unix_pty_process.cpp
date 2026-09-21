#include <draxul/unix_pty_process.h>

#include "agent_process_observer.h"
#include "unix_agent_process_probe.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <draxul/perf_timing.h>
#include <draxul/process_util.h>
#include <draxul/terminal_dimensions.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

#ifdef __APPLE__
#include <libproc.h>
#include <sys/sysctl.h>
#include <util.h>
#else
#include <pty.h>
#endif

namespace draxul
{

namespace
{

// Terminal children get a real terminal identity plus any caller overrides;
// the shared helper strips the parent's values of the same variables first.
std::vector<std::string> build_terminal_child_environment(
    const std::vector<std::pair<std::string, std::string>>& overrides)
{
    std::vector<std::pair<std::string, std::string>> extra_entries{
        { "COLORTERM", "truecolor" },
        { "TERM_PROGRAM", "draxul" },
    };
    extra_entries.insert(extra_entries.end(), overrides.begin(), overrides.end());
    return build_child_environment("xterm-256color", extra_entries);
}

std::string process_working_directory(pid_t pid)
{
    if (pid <= 0)
        return {};

#ifdef __APPLE__
    proc_vnodepathinfo info = {};
    const int bytes = proc_pidinfo(pid, PROC_PIDVNODEPATHINFO, 0, &info, sizeof(info));
    if (bytes != static_cast<int>(sizeof(info)) || info.pvi_cdir.vip_path[0] == '\0')
        return {};
    return info.pvi_cdir.vip_path;
#elif defined(__linux__)
    std::array<char, 4096> buffer = {};
    const std::string link = "/proc/" + std::to_string(static_cast<long long>(pid)) + "/cwd";
    const ssize_t len = readlink(link.c_str(), buffer.data(), buffer.size() - 1);
    if (len <= 0)
        return {};
    buffer[static_cast<size_t>(len)] = '\0';
    return buffer.data();
#else
    (void)pid;
    return {};
#endif
}

} // namespace

UnixPtyProcess::UnixPtyProcess() = default;

UnixPtyProcess::~UnixPtyProcess()
{
    shutdown();
}

bool UnixPtyProcess::spawn(const std::string& command, const std::vector<std::string>& args,
    const std::string& working_dir, std::function<void()> on_output_available,
    int initial_cols, int initial_rows, bool login_shell,
    const std::vector<std::pair<std::string, std::string>>& environment)
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
    const auto initial_dimensions = normalize_terminal_dimensions(initial_cols, initial_rows);
    ws.ws_col = static_cast<unsigned short>(initial_dimensions.cols);
    ws.ws_row = static_cast<unsigned short>(initial_dimensions.rows);
    std::vector<std::string> child_env = build_terminal_child_environment(environment);
    std::vector<std::string> exec_paths = resolve_exec_paths(command);
    std::string login_argv0 = login_shell ? "-" : "";
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
    const int master_flags = fcntl(master_fd_, F_GETFL, 0);
    if (master_flags >= 0)
        (void)fcntl(master_fd_, F_SETFL, master_flags | O_NONBLOCK);
    on_output_available_ = std::move(on_output_available);
    agent_observer_ = std::make_unique<detail::AgentProcessObserver>(
        [this] { return detail::capture_unix_agent_processes(master_fd_); },
        detail::AgentProcessObserver::WaitForChange{},
        [this, observed_group = pid_t{ -1 }]() mutable {
            const pid_t current
                = detail::unix_foreground_process_group(master_fd_);
            const bool changed = current != observed_group;
            observed_group = current;
            return changed;
        });
    {
        std::lock_guard output_lock(output_mutex_);
        reader_running_ = true;
    }
    reader_thread_ = std::thread([this]() { reader_main(); });
    return true;
}

void UnixPtyProcess::shutdown()
{
    PERF_MEASURE();
    if (agent_observer_)
        agent_observer_->stop();
    {
        std::lock_guard output_lock(output_mutex_);
        reader_running_ = false;
    }
    output_space_.notify_all();

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
    output_bytes_ = 0;
    agent_observer_.reset();
}

void UnixPtyProcess::request_close()
{
    PERF_MEASURE();
    {
        std::lock_guard output_lock(output_mutex_);
        reader_running_ = false;
    }
    output_space_.notify_all();

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

std::string UnixPtyProcess::current_working_directory() const
{
    update_exit_status();
    if (pid_ <= 0 || last_exit_code_.has_value())
        return {};
    return process_working_directory(pid_);
}

uint64_t UnixPtyProcess::process_id() const
{
    update_exit_status();
    return pid_ > 0 ? static_cast<uint64_t>(pid_) : 0;
}

std::optional<AgentProcessObservation>
UnixPtyProcess::foreground_process_observation() const
{
    update_exit_status();
    if (pid_ <= 0 || master_fd_ < 0 || !agent_observer_)
        return std::nullopt;
    agent_observer_->start();
    return agent_observer_->latest();
}

bool UnixPtyProcess::resize(int cols, int rows) const
{
    PERF_MEASURE();
    if (master_fd_ < 0)
        return false;
    struct winsize ws = {};
    const auto dimensions = normalize_terminal_dimensions(cols, rows);
    ws.ws_col = static_cast<unsigned short>(dimensions.cols);
    ws.ws_row = static_cast<unsigned short>(dimensions.rows);
    return ioctl(master_fd_, TIOCSWINSZ, &ws) == 0;
}

bool UnixPtyProcess::write(std::string_view text) const
{
    PERF_MEASURE();
    if (master_fd_ < 0)
        return false;
    const char* ptr = text.data();
    size_t remaining = text.size();
    while (remaining > 0 && reader_running_)
    {
        const ssize_t written = ::write(master_fd_, ptr, remaining);
        if (written < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                pollfd descriptor{ master_fd_, POLLOUT, 0 };
                const int ready = ::poll(&descriptor, 1, 50);
                if (ready >= 0)
                    continue;
                if (errno == EINTR)
                    continue;
            }
            return false; // real write error
        }
        if (written == 0)
            return false; // unexpected: write to PTY returned 0
        ptr += written;
        remaining -= static_cast<size_t>(written);
    }
    return remaining == 0;
}

std::vector<std::string> UnixPtyProcess::drain_output(
    bool* overflowed)
{
    PERF_MEASURE();
    std::scoped_lock lock(output_mutex_);
    std::vector<std::string> drained;
    drained.swap(output_chunks_);
    output_bytes_ = 0;
    if (overflowed)
        *overflowed = false;
    output_space_.notify_all();
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
                std::unique_lock lock(output_mutex_);
                const size_t chunk_bytes
                    = static_cast<size_t>(bytes_read);
                output_space_.wait(lock, [this, chunk_bytes] {
                    return !reader_running_
                        || output_bytes_
                        <= kMaxQueuedOutputBytes - chunk_bytes;
                });
                if (!reader_running_)
                    break;
                output_chunks_.emplace_back(buffer.data(), buffer.data() + bytes_read);
                output_bytes_ += chunk_bytes;
            }
            if (agent_observer_)
                agent_observer_->note_activity();

            if (on_output_available_)
                on_output_available_();
        }
        else if (fds[0].revents & (POLLHUP | POLLERR))
        {
            break;
        }
    }
    {
        std::lock_guard output_lock(output_mutex_);
        reader_running_ = false;
    }
    output_space_.notify_all();
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
