#pragma once

#include <draxul/agent_protocol.h>
#include <draxul/agent_instance_ids.h>
#include <draxul/control_plane.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace draxul
{

class AgentUsageMonitor;

struct ServerAgentRuntimeView
{
    std::string space_id;
    std::string tab_id;
    std::string pane_id;
    std::string terminal_id;
    std::optional<AgentIdentity> declared_identity;
    std::optional<AgentSessionRef> session_ref;
    // Directory a managed agent was launched in; discovered agents use the
    // probed process directory instead.
    std::string launch_working_directory;
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
    // usage_monitor attributes token activity from native session files;
    // without one, projections carry no activity.
    explicit ServerAgentService(std::string session_id);
    ServerAgentService(std::string session_id,
        std::unique_ptr<AgentUsageMonitor> usage_monitor);
    ~ServerAgentService();

    void reserve_instance_id(std::string_view identity) { instance_ids_.reserve(identity); }
    std::string allocate_instance_id() { return instance_ids_.next(); }

    void update(const std::vector<ServerAgentRuntimeView>& runtimes,
        std::chrono::steady_clock::time_point now
        = std::chrono::steady_clock::now());
    ControlMethodResult handle(
        std::string_view method, const nlohmann::json& params) const;

    // Records an official native session reference for a discovered agent
    // (one started by hand in a shell pane, which has no managed identity).
    // A report that arrives before process discovery is held briefly and
    // applied when an agent of the same kind is discovered on that terminal
    // runtime generation.
    ControlMethodResult report_discovered_session(std::string_view terminal_id,
        AgentRuntimeGeneration generation, const AgentSessionRef& session_ref,
        std::chrono::steady_clock::time_point now
        = std::chrono::steady_clock::now());

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
        std::string working_directory;
        std::chrono::steady_clock::time_point first_seen_at{};
        // Native session reported by an integration hook for a discovered
        // agent; managed agents keep theirs in Session topology.
        std::optional<AgentSessionRef> session_ref;
    };

    struct PendingSessionRef
    {
        AgentRuntimeGeneration generation{};
        AgentSessionRef session_ref;
        std::chrono::steady_clock::time_point reported_at{};
    };

    std::string session_id_;
    ServerAgentSnapshot snapshot_;
    std::unordered_map<std::string, RuntimeState> runtime_states_;
    std::unordered_map<std::string, PendingSessionRef> pending_session_refs_;
    AgentInstanceIds instance_ids_;
    std::unique_ptr<AgentUsageMonitor> usage_monitor_;
};

} // namespace draxul
