#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <draxul/server_protocol.h>

namespace draxul
{

struct ServerProbeResult
{
    ServerProbeState state = ServerProbeState::Absent;
    std::optional<ServerWelcome> welcome;
    std::string error_code;
    std::string error_message;

    bool ready() const
    {
        return state == ServerProbeState::Ready && welcome.has_value();
    }
};

struct ServerStatusResult
{
    bool ok = false;
    std::optional<ServerStatusSnapshot> status;
    std::string error_code;
    std::string error_message;
};

struct ServerEnsureOptions
{
    std::filesystem::path runtime_directory;
    std::filesystem::path executable_path;
    std::string client_id;
    int protocol_major = kServerProtocolMajor;
    int protocol_minor = kServerProtocolMinor;
    std::chrono::milliseconds timeout = std::chrono::seconds(10);
    std::chrono::milliseconds request_timeout
        = std::chrono::milliseconds(500);
    bool launch_if_missing = true;
    std::string terminal_shell_kind;
    std::string terminal_command;
    std::filesystem::path terminal_working_directory;
    int terminal_scrollback_lines = 10000;
};

struct ServerShutdownOptions
{
    // A server with live terminal processes refuses shutdown unless this is
    // explicit. Callers must obtain confirmation from the user first.
    bool confirm_live_terminals = false;
    std::chrono::milliseconds request_timeout
        = std::chrono::seconds(5);
};

struct ServerDeleteSessionOptions
{
    // Deleting a Session destroys every terminal process it owns. Callers
    // must obtain confirmation from the user first when any are still live.
    bool confirm_live_terminals = false;
};

std::filesystem::path server_runtime_directory(
    const std::filesystem::path& config_directory);
std::filesystem::path server_metadata_path(
    const std::filesystem::path& runtime_directory);
std::string make_server_client_id();

class ServerClient
{
public:
    static ServerProbeResult probe(const ServerEnsureOptions& options);
    static ServerProbeResult ensure(const ServerEnsureOptions& options);
    static ServerStatusResult status(
        const std::filesystem::path& runtime_directory,
        std::chrono::milliseconds request_timeout
        = std::chrono::seconds(5));
    static bool delete_session(
        const std::filesystem::path& runtime_directory,
        std::string_view session_id,
        const ServerDeleteSessionOptions& options,
        std::string& error);
    static bool rename_session(
        const std::filesystem::path& runtime_directory,
        std::string_view session_id,
        std::string_view session_name,
        std::string& error);
    static bool shutdown(const std::filesystem::path& runtime_directory,
        const ServerShutdownOptions& options, std::string& error);
    static bool disconnect(const std::filesystem::path& runtime_directory,
        std::string_view client_id, std::string& error);
    // Emergency recovery path. This bypasses checkpointing and therefore
    // requires an explicit confirmation from the caller.
    static bool force_stop(const std::filesystem::path& runtime_directory,
        bool confirmed, std::string& error);
    static bool launch_detached(
        const ServerEnsureOptions& options, std::string& error);
};

} // namespace draxul
