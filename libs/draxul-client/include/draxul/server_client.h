#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

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
    bool launch_if_missing = true;
    std::string terminal_shell_kind;
    std::filesystem::path terminal_working_directory;
    int terminal_scrollback_lines = 10000;
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
        const std::filesystem::path& runtime_directory);
    static bool shutdown(const std::filesystem::path& runtime_directory,
        std::string& error);
    static bool launch_detached(
        const ServerEnsureOptions& options, std::string& error);
};

} // namespace draxul
