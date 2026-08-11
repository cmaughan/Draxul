#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <draxul/agent_model.h>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
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
        int initial_cols = 80, int initial_rows = 24, bool login_shell = true,
        const std::vector<std::pair<std::string, std::string>>& environment = {});
    void shutdown();
    void request_close();
    bool is_running() const;
    uint64_t process_id() const;
    std::optional<int> exit_code() const;
    std::string current_working_directory() const;
    std::optional<AgentProcessObservation> foreground_process_observation() const;
    bool resize(int cols, int rows) const;
    bool write(std::string_view text) const;
    std::vector<std::string> drain_output(
        bool* overflowed = nullptr);

    static constexpr size_t kMaxQueuedOutputBytes = 1024 * 1024;

private:
    void reader_main();
    void update_exit_status() const;
    void ensure_agent_observer_started() const;
    void stop_agent_observer();
    void agent_observer_main() const;
    std::optional<AgentProcessObservation>
    capture_agent_process_observation_now(
        pid_t foreground_group) const;

    int master_fd_ = -1;
    int shutdown_pipe_[2] = { -1, -1 };
    mutable pid_t pid_ = -1;
    std::thread reader_thread_;
    std::atomic<bool> reader_running_{ false };
    std::mutex output_mutex_;
    std::condition_variable output_space_;
    std::vector<std::string> output_chunks_;
    size_t output_bytes_ = 0;
    std::function<void()> on_output_available_;
    mutable std::optional<int> last_exit_code_;
    mutable std::mutex agent_observer_start_mutex_;
    mutable std::mutex agent_observation_mutex_;
    mutable std::mutex agent_observer_wait_mutex_;
    mutable std::condition_variable agent_observer_wake_;
    mutable std::thread agent_observer_thread_;
    mutable std::atomic<bool> agent_observer_running_{ false };
    mutable std::atomic<uint64_t> agent_activity_generation_{ 0 };
    mutable std::optional<AgentProcessObservation>
        cached_agent_process_observation_;
};

} // namespace draxul
