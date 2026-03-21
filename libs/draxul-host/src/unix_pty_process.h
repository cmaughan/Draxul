#pragma once

#include <atomic>
#include <functional>
#include <mutex>
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
    bool is_running() const;
    bool resize(int cols, int rows) const;
    bool write(std::string_view text) const;
    std::vector<std::string> drain_output();

private:
    void reader_main();

    int master_fd_ = -1;
    pid_t pid_ = -1;
    std::thread reader_thread_;
    std::atomic<bool> reader_running_{ false };
    std::mutex output_mutex_;
    std::vector<std::string> output_chunks_;
    std::function<void()> on_output_available_;
};

} // namespace draxul
