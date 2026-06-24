#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace draxul
{

// Spawns a child process attached to a POSIX pseudo-terminal (PTY) and
// provides read/write access to it via a background reader thread.
// Used by ShellHost on macOS and Linux.
class UnixPtyProcess
{
public:
    ~UnixPtyProcess();

    bool spawn(const std::string& command, const std::vector<std::string>& args,
        const std::string& working_dir, std::function<void()> on_output_available,
        int initial_cols = 80, int initial_rows = 24);
    void shutdown();
    void request_close();
    bool is_running() const;
    std::optional<int> exit_code() const;
    std::string current_working_directory() const;
    bool resize(int cols, int rows) const;
    bool write(std::string_view text) const;
    std::vector<std::string> drain_output();

private:
    void reader_main();
    void update_exit_status() const;

    int master_fd_ = -1;
    int shutdown_pipe_[2] = { -1, -1 };
    mutable pid_t pid_ = -1;
    std::thread reader_thread_;
    std::atomic<bool> reader_running_{ false };
    std::mutex output_mutex_;
    std::vector<std::string> output_chunks_;
    std::function<void()> on_output_available_;
    mutable std::optional<int> last_exit_code_;
};

} // namespace draxul
