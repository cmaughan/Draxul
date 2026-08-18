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

inline constexpr int kServerProtocolMajor = 2;
inline constexpr int kServerProtocolMinor = 0;
inline constexpr std::string_view kServerControlId = "__draxul_server_v1__";
inline constexpr size_t kServerMaxCapabilities = 32;
inline constexpr size_t kServerMaxClientIdBytes = 128;
inline constexpr size_t kServerMaxConnectionTokenBytes = 512;
inline constexpr size_t kServerMaxSessionIdBytes = 512;
inline constexpr size_t kServerMaxConnectedClients = 128;
inline constexpr size_t kServerMaxSessions = 128;
inline constexpr size_t kServerMaxHandshakeTextBytes = 128;
inline constexpr size_t kServerMaxStatusStateBytes = 128;
inline constexpr size_t kServerMaxStatusAggregateCount = 1'000'000;
inline constexpr size_t kServerMaxStatusResourceCells = 1'000'000'000;
inline constexpr size_t kServerMaxStatusDetailBytes = 4096;
inline constexpr size_t kServerMaxRestoreWarnings = 64;
inline constexpr std::string_view kServerClientTokenCapability
    = "client-token-v1";

struct ServerHello
{
    int protocol_major = kServerProtocolMajor;
    int protocol_minor = kServerProtocolMinor;
    std::string client_id;
    std::string connection_token;
    std::string registration_nonce;
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
    std::string connection_token;
    std::vector<std::string> capabilities;

    bool operator==(const ServerWelcome&) const = default;
};

struct ServerSessionStatusSnapshot
{
    std::string session_id;
    std::string session_name;
    size_t spaces = 0;
    size_t terminals = 0;
    size_t live_terminals = 0;
    std::string checkpoint_path;
    std::string checkpoint_state;
    uint64_t last_checkpoint_unix_ms = 0;
    std::string checkpoint_error;
    std::vector<std::string> restore_warnings;

    bool operator==(const ServerSessionStatusSnapshot&) const = default;
};

struct ServerControlTimingSnapshot
{
    uint64_t samples = 0;
    uint64_t total_us = 0;
    uint64_t max_us = 0;

    bool operator==(const ServerControlTimingSnapshot&) const = default;
};

struct ServerControlMethodMetricsSnapshot
{
    std::string method;
    uint64_t requests = 0;
    uint64_t failures = 0;
    ServerControlTimingSnapshot queue_time;
    ServerControlTimingSnapshot dispatch_time;
    ServerControlTimingSnapshot response_time;

    bool operator==(const ServerControlMethodMetricsSnapshot&) const = default;
};

struct ServerControlFailureMetricsSnapshot
{
    std::string operation;
    std::string stage;
    std::string native_domain;
    std::string classification;
    uint32_t native_code = 0;
    uint64_t count = 0;

    bool operator==(const ServerControlFailureMetricsSnapshot&) const = default;
};

struct ServerControlMetricsSnapshot
{
    uint64_t listener_capacity = 0;
    uint64_t accepted_connections = 0;
    uint64_t active_connections = 0;
    uint64_t peak_connections = 0;
    uint64_t requests = 0;
    uint64_t failed_requests = 0;
    uint64_t invalid_frames = 0;
    std::vector<ServerControlMethodMetricsSnapshot> methods;
    std::vector<ServerControlFailureMetricsSnapshot> transport_failures;

    bool operator==(const ServerControlMetricsSnapshot&) const = default;
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
    size_t scrollback_cells_reserved = 0;
    size_t scrollback_cells_limit = 0;
    std::string checkpoint_path;
    std::string checkpoint_state;
    uint64_t last_checkpoint_unix_ms = 0;
    std::string checkpoint_error;
    std::vector<std::string> restore_warnings;
    std::vector<ServerSessionStatusSnapshot> session_statuses;
    ServerControlMetricsSnapshot control_transport;

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
bool valid_server_client_id(std::string_view value);

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
