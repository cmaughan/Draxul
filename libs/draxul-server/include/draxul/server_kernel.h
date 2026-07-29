#pragma once

#include <draxul/server_protocol.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace draxul
{

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
};

std::filesystem::path server_session_state_path(
    const std::filesystem::path& runtime_directory);

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
