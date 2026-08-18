#pragma once

#include <draxul/agent_protocol.h>
#include <draxul/control_plane.h>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace draxul
{

struct ServerAgentRuntimeView
{
    std::string space_id;
    std::string tab_id;
    std::string pane_id;
    std::string terminal_id;
    std::optional<AgentIdentity> declared_identity;
    std::optional<AgentSessionRef> session_ref;
    AgentRuntimeGeneration generation{};
    bool runtime_running = false;
    std::optional<int> exit_code;
    std::optional<AgentProcessObservation> process_observation;
    std::optional<AgentObservation> terminal_observation;
};

// Session-scoped authority for discovering and evaluating agents from
// server-owned terminal runtimes. This is a private server collaborator; tests
// reach it only through draxul-server-test-internals.
class ServerAgentService
{
public:
    explicit ServerAgentService(std::string session_id);

    void update(const std::vector<ServerAgentRuntimeView>& runtimes,
        std::chrono::steady_clock::time_point now
        = std::chrono::steady_clock::now());
    ControlMethodResult handle(
        std::string_view method, const nlohmann::json& params) const;

    const ServerAgentSnapshot& snapshot() const noexcept
    {
        return snapshot_;
    }

private:
    struct RuntimeState
    {
        AgentRuntimeGeneration generation{};
        std::optional<AgentIdentity> discovered_identity;
        std::string identity_evidence_category;
        bool identity_high_confidence = false;
        bool process_present = false;
        int failed_probes = 0;
        std::chrono::steady_clock::time_point detected_at{};
        AgentStatusExplanation explanation;
        bool attention = false;
        AgentStatus last_status = AgentStatus::Unknown;
    };

    std::string session_id_;
    ServerAgentSnapshot snapshot_;
    std::unordered_map<std::string, RuntimeState> runtime_states_;
    uint64_t next_instance_serial_ = 1;
};

} // namespace draxul
