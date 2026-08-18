#pragma once

#include <draxul/agent_protocol.h>
#include <draxul/remote_terminal_protocol.h>
#include <draxul/topology_protocol.h>

#include <cstddef>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>

namespace draxul
{

inline constexpr size_t kSessionPollMaxSubscriptions = 256;
inline constexpr size_t kSessionPollMaxTerminalIdBytes = 512;
inline constexpr size_t kSessionStreamMaxEndpointBytes = 1024;
inline constexpr size_t kSessionStreamMaxTicketBytes = 256;
inline constexpr size_t kSessionStreamMaxSessionIdBytes = 512;
inline constexpr size_t kSessionStreamMaxFrameBytes = 8 * 1024 * 1024;
inline constexpr size_t kSessionStreamMinQueueBytes = 256 * 1024;
inline constexpr uint32_t kSessionStreamMinHeartbeatIntervalMs = 100;
inline constexpr uint32_t kSessionStreamMaxHeartbeatIntervalMs = 60'000;
inline constexpr uint32_t kSessionStreamDefaultHeartbeatIntervalMs = 1000;
inline constexpr size_t kSessionStreamDefaultQueueBytes = 16 * 1024 * 1024;
inline constexpr size_t kSessionStreamMaxQueueBytes
    = kSessionStreamDefaultQueueBytes;

struct SessionTerminalCursor
{
    uint64_t generation = 0;
    uint64_t after_sequence = 0;

    bool operator==(const SessionTerminalCursor&) const = default;
};

struct SessionTerminalSubscription
{
    uint64_t subscription_id = 0;
    std::string terminal_id;
    uint64_t visibility_generation = 0;
    bool visible = true;
    std::optional<SessionTerminalCursor> cursor;

    bool operator==(const SessionTerminalSubscription&) const = default;
};

struct SessionPollRequest
{
    uint64_t request_serial = 0;
    std::string server_epoch;
    uint64_t topology_after_revision = 0;
    uint64_t agent_after_revision = 0;
    std::vector<SessionTerminalSubscription> terminals;

    bool operator==(const SessionPollRequest&) const = default;
};

struct SessionTopologyPollChannel
{
    uint64_t revision = 0;
    std::optional<TopologySnapshot> snapshot;
    bool resync = false;
    bool deferred = false;
    std::string error_code;
    std::string error_message;

    bool operator==(const SessionTopologyPollChannel&) const = default;
};

struct SessionAgentPollChannel
{
    uint64_t revision = 0;
    std::optional<ServerAgentSnapshot> snapshot;
    bool resync = false;
    bool deferred = false;
    std::string error_code;
    std::string error_message;

    bool operator==(const SessionAgentPollChannel&) const = default;
};

struct SessionTerminalPollBatch
{
    uint64_t subscription_id = 0;
    std::string terminal_id;
    uint64_t visibility_generation = 0;
    std::optional<RemoteTerminalAttach> attach;
    std::vector<RemoteTerminalEvent> events;
    bool suspended = false;
    bool resync = false;
    bool more = false;
    std::string error_code;
    std::string error_message;

    bool operator==(const SessionTerminalPollBatch&) const = default;
};

struct SessionPollResponse
{
    uint64_t request_serial = 0;
    std::string server_epoch;
    SessionTopologyPollChannel topology;
    SessionAgentPollChannel agents;
    std::vector<SessionTerminalPollBatch> terminals;
    bool more = false;

    bool operator==(const SessionPollResponse&) const = default;
};

struct SessionStreamOpenRequest
{
    std::string server_epoch;
    std::string session_id;
    SessionPollRequest poll;

    bool operator==(const SessionStreamOpenRequest&) const = default;
};

struct SessionStreamOpenResponse
{
    std::string server_epoch;
    std::string endpoint;
    std::string ticket;
    uint32_t heartbeat_interval_ms = kSessionStreamDefaultHeartbeatIntervalMs;
    size_t max_frame_bytes = 0;
    size_t max_queue_bytes = kSessionStreamDefaultQueueBytes;

    bool operator==(const SessionStreamOpenResponse&) const = default;
};

struct SessionStreamConnectRequest
{
    std::string server_epoch;
    std::string ticket;

    bool operator==(const SessionStreamConnectRequest&) const = default;
};

struct SessionStreamUpdate
{
    SessionPollRequest poll;

    bool operator==(const SessionStreamUpdate&) const = default;
};

enum class SessionStreamClientFrameKind
{
    Connect,
    Update,
    Close,
};

struct SessionStreamClientFrame
{
    SessionStreamClientFrameKind kind = SessionStreamClientFrameKind::Connect;
    std::optional<SessionStreamConnectRequest> connect;
    std::optional<SessionStreamUpdate> update;

    bool operator==(const SessionStreamClientFrame&) const = default;
};

enum class SessionStreamServerFrameKind
{
    Events,
    Heartbeat,
    Error,
};

struct SessionStreamServerFrame
{
    SessionStreamServerFrameKind kind = SessionStreamServerFrameKind::Heartbeat;
    uint64_t frame_serial = 0;
    std::string server_epoch;
    std::optional<SessionPollResponse> events;
    std::string error_code;
    std::string error_message;

    bool operator==(const SessionStreamServerFrame&) const = default;
};

nlohmann::json session_poll_request_to_json(
    const SessionPollRequest& request);
std::optional<SessionPollRequest> session_poll_request_from_json(
    const nlohmann::json& value, std::string& error);
nlohmann::json session_poll_response_to_json(
    const SessionPollResponse& response);
std::optional<SessionPollResponse> session_poll_response_from_json(
    const nlohmann::json& value, std::string& error);
nlohmann::json session_stream_open_request_to_json(
    const SessionStreamOpenRequest& request);
std::optional<SessionStreamOpenRequest> session_stream_open_request_from_json(
    const nlohmann::json& value, std::string& error);
nlohmann::json session_stream_open_response_to_json(
    const SessionStreamOpenResponse& response);
std::optional<SessionStreamOpenResponse> session_stream_open_response_from_json(
    const nlohmann::json& value, std::string& error);
nlohmann::json session_stream_client_frame_to_json(
    const SessionStreamClientFrame& frame);
std::optional<SessionStreamClientFrame> session_stream_client_frame_from_json(
    const nlohmann::json& value, std::string& error);
nlohmann::json session_stream_server_frame_to_json(
    const SessionStreamServerFrame& frame);
std::optional<SessionStreamServerFrame> session_stream_server_frame_from_json(
    const nlohmann::json& value, std::string& error);

} // namespace draxul
