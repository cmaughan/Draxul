#pragma once

#include <draxul/server_protocol.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
std::string format_server_session_listing_table(
    const std::vector<ServerSessionStatusSnapshot>& sessions);
std::filesystem::path default_server_log_path(
    const std::filesystem::path& runtime_directory);

enum class ServerStatusAction
{
    Stop,
    ForceStop,
};

// Tray callbacks run synchronously from the SDL event pump. Confirmation must
// therefore remain non-modal so server-thread completion can always return the
// process to its shutdown path.
class ServerStatusConfirmation
{
public:
    bool request(ServerStatusAction action,
        std::chrono::steady_clock::time_point now);
    bool expire(
        std::chrono::steady_clock::time_point now);
    void clear();
    std::optional<ServerStatusAction> pending() const;

private:
    std::optional<ServerStatusAction> pending_;
    std::chrono::steady_clock::time_point expires_at_{};
};

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
