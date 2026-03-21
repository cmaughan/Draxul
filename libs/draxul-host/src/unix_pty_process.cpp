#include "unix_pty_process.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <fcntl.h>
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

namespace draxul
{

UnixPtyProcess::~UnixPtyProcess()
{
    shutdown();
}

bool UnixPtyProcess::spawn(const std::string& command, const std::vector<std::string>& args,
    const std::string& working_dir, std::function<void()> on_output_available,
    int initial_cols, int initial_rows)
{
    shutdown();

    // Suppress SIGPIPE so writes to a closed PTY master return EPIPE instead
    // of delivering a fatal signal. Safe for a GUI application.
    signal(SIGPIPE, SIG_IGN);

    struct winsize ws = {};
    ws.ws_col = static_cast<unsigned short>(std::clamp(initial_cols, 1, 320));
    ws.ws_row = static_cast<unsigned short>(std::clamp(initial_rows, 1, 200));

    int slave_fd = -1;
    if (openpty(&master_fd_, &slave_fd, nullptr, nullptr, &ws) < 0)
        return false;

    pid_ = fork();
    if (pid_ < 0)
    {
        close(slave_fd);
        close(master_fd_);
        master_fd_ = -1;
        return false;
    }

    if (pid_ == 0)
    {
        // Child process: become session leader, attach PTY, exec the shell.
        close(master_fd_);
        setsid();

        if (ioctl(slave_fd, TIOCSCTTY, 0) < 0)
            _exit(127);

        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);
        if (slave_fd > STDERR_FILENO)
            close(slave_fd);

        if (!working_dir.empty())
            chdir(working_dir.c_str());

        // POSIX convention: argv[0] starting with '-' signals a login shell so
        // the shell sources its full profile (e.g. ~/.zprofile, /etc/zprofile).
        // This gives the same $PATH the user sees in a normal terminal window.
        std::string login_argv0 = "-";
        const auto slash = command.rfind('/');
        login_argv0 += (slash == std::string::npos) ? command : command.substr(slash + 1);

        std::vector<const char*> argv;
        argv.push_back(login_argv0.c_str());
        for (const auto& a : args)
            argv.push_back(a.c_str());
        argv.push_back(nullptr);

        execvp(command.c_str(), const_cast<char**>(argv.data())); // NOSONAR — POSIX execvp takes char*const*; argv holds c_str() pointers that are not modified
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
    reader_running_ = false;

    if (master_fd_ >= 0)
    {
        close(master_fd_);
        master_fd_ = -1;
    }

    if (reader_thread_.joinable())
        reader_thread_.join();

    if (pid_ > 0)
    {
        int status = 0;
        if (waitpid(pid_, &status, WNOHANG) == 0)
        {
            kill(pid_, SIGTERM);
            for (int i = 0; i < 10 && waitpid(pid_, &status, WNOHANG) == 0; ++i)
                std::this_thread::sleep_for(std::chrono::microseconds(10000));
            kill(pid_, SIGKILL);
            waitpid(pid_, &status, 0);
        }
        pid_ = -1;
    }

    std::scoped_lock lock(output_mutex_);
    output_chunks_.clear();
}

bool UnixPtyProcess::is_running() const
{
    if (pid_ <= 0)
        return false;
    int status = 0;
    return waitpid(pid_, &status, WNOHANG) == 0;
}

bool UnixPtyProcess::resize(int cols, int rows) const
{
    if (master_fd_ < 0)
        return false;
    struct winsize ws = {};
    ws.ws_col = static_cast<unsigned short>(std::clamp(cols, 1, 320));
    ws.ws_row = static_cast<unsigned short>(std::clamp(rows, 1, 200));
    return ioctl(master_fd_, TIOCSWINSZ, &ws) == 0;
}

bool UnixPtyProcess::write(std::string_view text) const
{
    if (master_fd_ < 0)
        return false;
    const char* ptr = text.data();
    size_t remaining = text.size();
    while (remaining > 0)
    {
        const ssize_t written = ::write(master_fd_, ptr, remaining);
        if (written <= 0)
            return false;
        ptr += written;
        remaining -= static_cast<size_t>(written);
    }
    return true;
}

std::vector<std::string> UnixPtyProcess::drain_output()
{
    std::scoped_lock lock(output_mutex_);
    std::vector<std::string> drained;
    drained.swap(output_chunks_);
    return drained;
}

void UnixPtyProcess::reader_main()
{
    std::array<char, 4096> buffer{};
    while (reader_running_)
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
}

} // namespace draxul
