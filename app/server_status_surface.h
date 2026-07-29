#pragma once

#include <draxul/server_protocol.h>

#include <filesystem>
#include <memory>
#include <string>

namespace draxul
{

struct ServerStatusText
{
    std::string state;
    std::string clients_and_terminals;
    std::string sessions_spaces_and_agents;
};

ServerStatusText format_server_status_text(
    const ServerStatusSnapshot& status);
std::string format_server_status_summary(
    const ServerStatusSnapshot& status);
std::filesystem::path default_server_log_path(
    const std::filesystem::path& runtime_directory);

bool launch_draxul_ui(const std::filesystem::path& executable,
    const std::filesystem::path& runtime_directory,
    std::string& error);
bool open_server_log(
    const std::filesystem::path& log_path, std::string& error);

struct ServerStatusSurfaceOptions
{
    std::filesystem::path runtime_directory;
    std::filesystem::path executable_path;
    std::filesystem::path log_path;
};

// Optional native notification-area/menu-bar projection for the headless
// server. Failure to create it is non-fatal; the server remains operable
// through the CLI and control endpoint.
class ServerStatusSurface
{
public:
    explicit ServerStatusSurface(
        ServerStatusSurfaceOptions options);
    ~ServerStatusSurface();
    ServerStatusSurface(const ServerStatusSurface&) = delete;
    ServerStatusSurface& operator=(const ServerStatusSurface&) = delete;

    bool initialize(std::string& error);
    void pump();
    void shutdown();
    bool available() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace draxul
