#pragma once

#include <draxul/agent_model.h>

#include <cstddef>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>

namespace draxul
{

inline constexpr size_t kServerAgentMaxRows = 1024;
inline constexpr size_t kServerAgentMaxTextBytes = 4096;
inline constexpr size_t kServerAgentMaxWaitStates = 16;
inline constexpr size_t kServerAgentMaxWaitStateBytes = 64;

// Sanitized, renderer-free agent state published by the server. Raw terminal
// observations and process command lines deliberately never cross this
// boundary.
struct ServerAgentProjection
{
    std::string space_id;
    std::string tab_id;
    std::string pane_id;
    std::string terminal_id;
    AgentIdentity identity;
    std::string identity_evidence_category;
    bool identity_high_confidence = true;
    std::optional<AgentSessionRef> session_ref;
    AgentLifecycle lifecycle = AgentLifecycle::Starting;
    AgentRuntimeGeneration generation{};
    std::optional<int> exit_code;
    AgentStatus status = AgentStatus::Unknown;
    AgentStateAuthority status_authority = AgentStateAuthority::None;
    std::string manifest_id;
    uint32_t manifest_version = 0;
    std::string rule_id;
    std::string status_evidence_category;
    std::string fallback_reason;
    uint64_t observation_generation = 0;
    bool attention = false;
    bool running = false;

    bool operator==(const ServerAgentProjection&) const = default;
};

struct ServerAgentSnapshot
{
    uint64_t revision = 0;
    std::string session_id;
    std::vector<ServerAgentProjection> agents;

    bool operator==(const ServerAgentSnapshot&) const = default;
};

nlohmann::json server_agent_snapshot_to_json(
    const ServerAgentSnapshot& snapshot);
std::optional<ServerAgentSnapshot> server_agent_snapshot_from_json(
    const nlohmann::json& value, std::string& error);

} // namespace draxul
