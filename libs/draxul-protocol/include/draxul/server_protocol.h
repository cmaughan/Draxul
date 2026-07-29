#pragma once

#include <cstddef>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

inline constexpr int kServerProtocolMajor = 1;
inline constexpr int kServerProtocolMinor = 0;
inline constexpr std::string_view kServerControlId = "__draxul_server_v1__";
inline constexpr size_t kServerMaxCapabilities = 32;
inline constexpr size_t kServerMaxClientIdBytes = 128;

struct ServerHello
{
    int protocol_major = kServerProtocolMajor;
    int protocol_minor = kServerProtocolMinor;
    std::string client_id;
    std::vector<std::string> capabilities;

    bool operator==(const ServerHello&) const = default;
};

struct ServerWelcome
{
    int protocol_major = 0;
    int protocol_minor = 0;
    uint64_t server_pid = 0;
    std::string server_epoch;
    std::string build_version;
    std::vector<std::string> capabilities;

    bool operator==(const ServerWelcome&) const = default;
};

struct ServerStatusSnapshot
{
    std::string state;
    int protocol_major = 0;
    int protocol_minor = 0;
    uint64_t server_pid = 0;
    std::string server_epoch;
    std::string build_version;
    uint64_t uptime_ms = 0;
    size_t connected_clients = 0;
    size_t sessions = 0;
    size_t spaces = 0;
    size_t terminals = 0;
    size_t agents = 0;
    std::string checkpoint_path;
    std::string checkpoint_state;
    uint64_t last_checkpoint_unix_ms = 0;
    std::string checkpoint_error;
    std::vector<std::string> restore_warnings;

    bool operator==(const ServerStatusSnapshot&) const = default;
};

enum class ServerProbeState
{
    Absent,
    Starting,
    Ready,
    Busy,
    Incompatible,
    Crashed,
    Stale,
    LaunchFailed,
};

std::string_view to_string(ServerProbeState state);
std::string server_build_version();

nlohmann::json server_hello_to_json(const ServerHello& hello);
std::optional<ServerHello> server_hello_from_json(
    const nlohmann::json& value, std::string& error);
nlohmann::json server_welcome_to_json(const ServerWelcome& welcome);
std::optional<ServerWelcome> server_welcome_from_json(
    const nlohmann::json& value, std::string& error);
nlohmann::json server_status_to_json(const ServerStatusSnapshot& status);
std::optional<ServerStatusSnapshot> server_status_from_json(
    const nlohmann::json& value, std::string& error);

} // namespace draxul
