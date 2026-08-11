#include <draxul/unix_pty_process.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <draxul/perf_timing.h>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
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

extern char** environ;

namespace draxul
{

namespace
{

bool starts_with(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::vector<std::string> build_child_environment(
    const std::vector<std::pair<std::string, std::string>>& overrides)
{
    std::vector<std::string> env;
    for (char** current = environ; current != nullptr && *current != nullptr; ++current)
    {
        const std::string_view entry(*current);
        const size_t equals = entry.find('=');
        const std::string_view key = entry.substr(0, equals);
        if (starts_with(entry, "TERM=")
            || starts_with(entry, "COLORTERM=")
            || starts_with(entry, "TERM_PROGRAM=")
            || std::any_of(overrides.begin(), overrides.end(),
                [&](const auto& value) { return value.first == key; }))
        {
            continue;
        }
        env.emplace_back(entry);
    }

    env.emplace_back("TERM=xterm-256color");
    env.emplace_back("COLORTERM=truecolor");
    env.emplace_back("TERM_PROGRAM=draxul");
    for (const auto& [key, value] : overrides)
        env.push_back(key + "=" + value);
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

#ifdef __linux__
std::vector<std::string> read_null_separated_file(
    const std::filesystem::path& path, size_t max_bytes)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    std::string contents(max_bytes, '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    contents.resize(static_cast<size_t>(input.gcount()));
    std::vector<std::string> values;
    size_t offset = 0;
    while (offset < contents.size() && values.size() < 64)
    {
        const size_t end = contents.find('\0', offset);
        const size_t length =
            (end == std::string::npos ? contents.size() : end) - offset;
        if (length != 0)
            values.emplace_back(contents.substr(offset, length));
        if (end == std::string::npos)
            break;
        offset = end + 1;
    }
    return values;
}
#endif

#ifdef __APPLE__
void read_macos_arguments_and_hint(pid_t process_id,
    std::vector<std::string>* arguments, std::string* hint)
{
    int argument_max = 0;
    size_t argument_max_size = sizeof(argument_max);
    int argument_max_mib[] = { CTL_KERN, KERN_ARGMAX };
    if (sysctl(argument_max_mib, 2, &argument_max, &argument_max_size,
            nullptr, 0)
            != 0
        || argument_max <= 0)
        return;
    std::vector<char> buffer(
        std::min<size_t>(static_cast<size_t>(argument_max), 64 * 1024));
    size_t size = buffer.size();
    int arguments_mib[] = { CTL_KERN, KERN_PROCARGS2, process_id };
    if (sysctl(arguments_mib, 3, buffer.data(), &size, nullptr, 0) != 0
        || size <= sizeof(int))
        return;

    int argument_count = 0;
    std::memcpy(&argument_count, buffer.data(), sizeof(argument_count));
    size_t offset = sizeof(argument_count);
    while (offset < size && buffer[offset] != '\0')
        ++offset;
    while (offset < size && buffer[offset] == '\0')
        ++offset;

    for (int index = 0;
         index < argument_count && index < 64 && offset < size; ++index)
    {
        const size_t end =
            std::find(buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                buffer.begin() + static_cast<std::ptrdiff_t>(size), '\0')
            - buffer.begin();
        if (end > offset && arguments)
            arguments->emplace_back(buffer.data() + offset, end - offset);
        offset = end + 1;
    }
    while (offset < size)
    {
        while (offset < size && buffer[offset] == '\0')
            ++offset;
        if (offset >= size)
            break;
        const size_t end =
            std::find(buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                buffer.begin() + static_cast<std::ptrdiff_t>(size), '\0')
            - buffer.begin();
        const std::string_view value(buffer.data() + offset, end - offset);
        constexpr std::string_view prefix = "DRAXUL_AGENT=";
        if (value.starts_with(prefix) && hint)
        {
            *hint = value.substr(prefix.size());
            break;
        }
        offset = end + 1;
    }
}
#endif

} // namespace

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
    ws.ws_col = static_cast<unsigned short>(std::clamp(initial_cols, 1, 320));
    ws.ws_row = static_cast<unsigned short>(std::clamp(initial_rows, 1, 200));
    std::vector<std::string> child_env = build_child_environment(environment);
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
    reader_running_ = true;
    reader_thread_ = std::thread([this]() { reader_main(); });
    return true;
}

void UnixPtyProcess::shutdown()
{
    PERF_MEASURE();
    stop_agent_observer();
    reader_running_ = false;
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
    {
        std::lock_guard observation_lock(
            agent_observation_mutex_);
        cached_agent_process_observation_.reset();
    }
    agent_activity_generation_ = 0;
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
    if (pid_ <= 0 || master_fd_ < 0)
        return std::nullopt;
    ensure_agent_observer_started();
    std::lock_guard lock(agent_observation_mutex_);
    return cached_agent_process_observation_;
}

std::optional<AgentProcessObservation>
UnixPtyProcess::capture_agent_process_observation_now(
    pid_t foreground_group) const
{
    if (foreground_group <= 0)
        return std::nullopt;
    AgentProcessObservation observation;
    observation.captured_at = std::chrono::steady_clock::now();
    observation.foreground_reliable = true;

#ifdef __APPLE__
    const int bytes =
        proc_listpids(PROC_PGRP_ONLY, static_cast<uint32_t>(foreground_group),
            nullptr, 0);
    if (bytes <= 0)
        return observation;
    std::vector<pid_t> process_ids(
        static_cast<size_t>(bytes) / sizeof(pid_t) + 8, 0);
    const int written =
        proc_listpids(PROC_PGRP_ONLY, static_cast<uint32_t>(foreground_group),
            process_ids.data(),
            static_cast<int>(process_ids.size() * sizeof(pid_t)));
    const size_t count =
        written > 0 ? static_cast<size_t>(written) / sizeof(pid_t) : 0;
    for (size_t index = 0;
         index < count && observation.processes.size() < 128; ++index)
    {
        const pid_t process_id = process_ids[index];
        if (process_id <= 0)
            continue;
        proc_bsdinfo info = {};
        if (proc_pidinfo(process_id, PROC_PIDTBSDINFO, 0, &info, sizeof(info))
            != static_cast<int>(sizeof(info)))
            continue;
        std::array<char, PROC_PIDPATHINFO_MAXSIZE> path = {};
        const int path_length =
            proc_pidpath(process_id, path.data(), static_cast<uint32_t>(path.size()));
        std::vector<std::string> arguments;
        std::string hint;
        read_macos_arguments_and_hint(process_id, &arguments, &hint);
        observation.processes.push_back({
            .process_id = static_cast<uint64_t>(process_id),
            .parent_process_id = static_cast<uint64_t>(info.pbi_ppid),
            .executable = path_length > 0 ? std::string(path.data())
                                         : std::string(info.pbi_name),
            .arguments = std::move(arguments),
            .agent_hint = std::move(hint),
        });
    }
#elif defined(__linux__)
    std::error_code ec;
    for (const auto& directory :
        std::filesystem::directory_iterator("/proc", ec))
    {
        if (ec || observation.processes.size() >= 128)
            break;
        const std::string name = directory.path().filename().string();
        if (name.empty()
            || !std::all_of(name.begin(), name.end(),
                [](unsigned char ch) { return std::isdigit(ch) != 0; }))
            continue;
        const pid_t process_id =
            static_cast<pid_t>(std::strtol(name.c_str(), nullptr, 10));
        std::ifstream stat(directory.path() / "stat");
        std::string stat_line;
        std::getline(stat, stat_line);
        const size_t close = stat_line.rfind(')');
        if (close == std::string::npos || close + 2 >= stat_line.size())
            continue;
        std::istringstream fields(stat_line.substr(close + 2));
        char state = '\0';
        pid_t parent_process_id = 0;
        pid_t process_group = 0;
        fields >> state >> parent_process_id >> process_group;
        if (!fields || process_group != foreground_group)
            continue;

        std::string executable;
        const auto executable_path =
            std::filesystem::read_symlink(directory.path() / "exe", ec);
        if (!ec)
            executable = executable_path.string();
        ec.clear();
        auto arguments =
            read_null_separated_file(directory.path() / "cmdline", 16 * 1024);
        std::string hint;
        for (const auto& value :
            read_null_separated_file(directory.path() / "environ", 64 * 1024))
        {
            constexpr std::string_view prefix = "DRAXUL_AGENT=";
            if (value.starts_with(prefix))
            {
                hint = value.substr(prefix.size());
                break;
            }
        }
        observation.processes.push_back({
            .process_id = static_cast<uint64_t>(process_id),
            .parent_process_id = static_cast<uint64_t>(parent_process_id),
            .executable = std::move(executable),
            .arguments = std::move(arguments),
            .agent_hint = std::move(hint),
        });
    }
#endif
    return observation;
}

void UnixPtyProcess::ensure_agent_observer_started() const
{
    std::lock_guard start_lock(agent_observer_start_mutex_);
    if (agent_observer_thread_.joinable()
        || pid_ <= 0 || master_fd_ < 0)
        return;
    agent_observer_running_ = true;
    agent_observer_thread_
        = std::thread([this] { agent_observer_main(); });
}

void UnixPtyProcess::stop_agent_observer()
{
    std::lock_guard start_lock(agent_observer_start_mutex_);
    agent_observer_running_ = false;
    agent_observer_wake_.notify_all();
    if (agent_observer_thread_.joinable())
        agent_observer_thread_.join();
}

void UnixPtyProcess::agent_observer_main() const
{
    constexpr auto change_debounce
        = std::chrono::seconds(1);
    constexpr auto present_reconcile
        = std::chrono::seconds(5);
    constexpr auto missing_reconcile
        = std::chrono::seconds(30);
    constexpr auto foreground_check
        = std::chrono::seconds(1);

    pid_t observed_group = -1;
    uint64_t observed_activity
        = agent_activity_generation_.load();
    bool dirty = true;
    bool agent_present = false;
    auto refresh_at = std::chrono::steady_clock::now();
    auto reconcile_at = refresh_at;
    bool first_probe = true;
    while (agent_observer_running_)
    {
        if (!first_probe)
        {
            std::unique_lock lock(agent_observer_wait_mutex_);
            agent_observer_wake_.wait_for(
                lock, foreground_check, [this] {
                    return !agent_observer_running_.load();
                });
        }
        first_probe = false;
        if (!agent_observer_running_)
            break;

        const auto now = std::chrono::steady_clock::now();
        const pid_t foreground_group
            = master_fd_ >= 0 ? tcgetpgrp(master_fd_) : -1;
        const uint64_t activity
            = agent_activity_generation_.load();
        if (foreground_group != observed_group
            || (!agent_present && activity != observed_activity))
        {
            if (observed_group < 0)
                refresh_at = now;
            else if (!dirty)
                refresh_at = now + change_debounce;
            dirty = true;
            observed_group = foreground_group;
            observed_activity = activity;
        }
        if ((!dirty || now < refresh_at)
            && now < reconcile_at)
        {
            continue;
        }

        auto observation
            = capture_agent_process_observation_now(
                foreground_group);
        agent_present = observation
            && discover_agent_process(*observation).has_value();
        {
            std::lock_guard lock(agent_observation_mutex_);
            cached_agent_process_observation_
                = std::move(observation);
        }
        dirty = false;
        observed_activity = activity;
        reconcile_at = now
            + (agent_present
                    ? present_reconcile
                    : missing_reconcile);
    }
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
            ++agent_activity_generation_;
            agent_observer_wake_.notify_one();

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
