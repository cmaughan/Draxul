#pragma once

#include "space_id.h"
#include "split_tree.h"
#include <draxul/agent_model.h>

#include <string>
#include <string_view>
#include <optional>
#include <chrono>
#include <unordered_map>
#include <vector>

namespace draxul
{

class SpaceController;

struct AgentProjection
{
    SpaceId space_id = kInvalidSpaceId;
    int tab_id = -1;
    LeafId leaf_id = kInvalidLeaf;
    std::string pane_id;
    AgentIdentity identity;
    std::string identity_evidence_category;
    bool identity_high_confidence = true;
    std::optional<AgentSessionRef> session_ref;
    AgentLifecycle lifecycle = AgentLifecycle::Starting;
    AgentRuntimeGeneration generation{};
    std::chrono::steady_clock::time_point runtime_started_at{};
    std::chrono::steady_clock::time_point lifecycle_transition_at{};
    std::optional<int> exit_code;
    AgentStatus status = AgentStatus::Unknown;
    AgentStateAuthority status_authority = AgentStateAuthority::None;
    AgentStatusExplanation status_explanation;
    bool attention = false;
    std::chrono::steady_clock::time_point last_status_transition_at{};
    bool running = false;
    bool focused = false;
};

// Derives the Agents index from pane-owned identities and resolves UI
// navigation back to the owning Space, tab, and pane.
class AgentController
{
public:
    std::vector<AgentProjection> query(SpaceController& spaces);
    bool focus(SpaceController& spaces, std::string_view instance_id) const;
    bool focus_by_index(SpaceController& spaces, int one_based_index);
    bool attach_focused(
        SpaceController& spaces, const AgentDefinition& definition);
    bool dismiss_focused(SpaceController& spaces);

private:
    struct CachedSemanticState
    {
        AgentRuntimeGeneration runtime_generation{};
        AgentLifecycle lifecycle = AgentLifecycle::Starting;
        std::chrono::steady_clock::time_point lifecycle_transition_at{};
        AgentStatusExplanation explanation;
        bool attention = false;
        std::chrono::steady_clock::time_point last_transition_at{};
    };

    struct CachedDiscoveryState
    {
        AgentRuntimeGeneration runtime_generation{};
        std::chrono::steady_clock::time_point last_probe_at{};
        std::chrono::steady_clock::time_point detected_at{};
        std::string kind;
        std::string evidence_category;
        bool high_confidence = false;
        int failed_probes = 0;
        int dismissed_absent_probes = 0;
        bool process_present = false;
        bool manual_override = false;
        std::string dismissed_kind;
    };

    std::unordered_map<std::string, CachedSemanticState> semantic_state_;
    std::unordered_map<std::string, CachedDiscoveryState> discovery_state_;
    uint64_t next_discovered_instance_ = 1;
};

} // namespace draxul
