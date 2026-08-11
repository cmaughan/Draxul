#pragma once

#include <draxul/agent_model.h>
#include <draxul/server_protocol.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace draxul
{

struct SessionSnapshot;

inline constexpr size_t kServerMaxTerminals = 256;
inline constexpr size_t kServerMaxScrollbackCells = 24'000'000;

enum class ServerStartDisposition
{
    Started,
    AlreadyRunning,
    Failed,
};

struct ServerStartResult
{
    ServerStartDisposition disposition = ServerStartDisposition::Failed;
    std::string error;
};

struct ServerKernelOptions
{
    std::filesystem::path runtime_directory;
    std::filesystem::path session_state_file;
    std::chrono::milliseconds session_checkpoint_interval{
        std::chrono::seconds(30)
    };
    std::chrono::milliseconds client_activity_timeout{
        std::chrono::seconds(10)
    };
    // How often the kernel verifies it is still the PUBLISHED server. A
    // server whose metadata was removed or replaced (wiped runtime dir, a
    // newer server claiming the endpoint) used to run forever — invisible to
    // clients, unreachable by the CLI, a tray icon its only surface — and
    // every later launch then added another one. Two consecutive failed
    // checks retire the kernel gracefully (single misses tolerate transient
    // filesystem states).
    std::chrono::milliseconds eviction_check_interval{
        std::chrono::seconds(5)
    };
    int protocol_major = kServerProtocolMajor;
    int protocol_minor = kServerProtocolMinor;
    std::string build_version;
    std::string epoch_override;
    std::string terminal_shell_kind;
    std::string terminal_command;
    std::vector<std::string> terminal_args;
    std::string terminal_working_directory;
    std::vector<std::pair<std::string, std::string>>
        terminal_environment;
    int terminal_scrollback_lines = 10000;
    size_t max_terminals = kServerMaxTerminals;
    size_t max_scrollback_cells = kServerMaxScrollbackCells;
    std::vector<AgentDefinition> agent_definitions;
    bool agents_resume_on_restore = false;
    std::chrono::milliseconds checkpoint_shutdown_budget{
        std::chrono::seconds(2)
    };
    std::function<bool(const SessionSnapshot&,
        const std::filesystem::path&, std::string*)>
        checkpoint_save;
    // Optional transport-health source used by deterministic tests. Production
    // kernels read listener failures directly from ControlServer.
    std::function<uint32_t()> listener_error_source;
};

std::filesystem::path server_session_state_path(
    const std::filesystem::path& runtime_directory);
std::filesystem::path server_session_state_path(
    const std::filesystem::path& runtime_directory,
    std::string_view session_id);

class ServerKernel
{
public:
    explicit ServerKernel(ServerKernelOptions options);
    ~ServerKernel();
    ServerKernel(const ServerKernel&) = delete;
    ServerKernel& operator=(const ServerKernel&) = delete;

    ServerStartResult start();
    int run_until_stopped();
    void request_stop();
    void stop();

    bool running() const;
    const std::string& epoch() const;
    uint64_t process_id() const;
    ServerStatusSnapshot status_snapshot() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace draxul
