#pragma once

#include "space_id.h"
#include "split_tree.h"
#include <draxul/agent_model.h>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
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
    // Always recomputes. Use this when the caller needs current truth and is
    // not on the frame path: the control plane, and any mutation that must
    // observe its own effect.
    std::vector<AgentProjection> query(SpaceController& spaces);

    // The frame-scoped view of the same projection. Recomputing walks every
    // pane, captures a bounded terminal observation, and (rate-limited) probes
    // the pane's process tree, so the chrome layout and its hit tests share
    // ONE evaluation per frame instead of repeating it 2-4 times.
    // begin_frame() drops it; a mutation that must appear within the current
    // frame calls invalidate(). A missed invalidate costs one frame of
    // staleness, never a wrong steady state.
    void begin_frame();
    void invalidate();
    void set_server_agents(
        std::vector<AgentProjection> agents);
    void clear_server_agents();
    const std::vector<AgentProjection>& frame_agents(SpaceController& spaces);
    bool focus(SpaceController& spaces, std::string_view instance_id) const;
    bool focus_by_index(SpaceController& spaces, int one_based_index);
    bool attach_focused(
        SpaceController& spaces, const AgentDefinition& definition);
    bool dismiss_focused(SpaceController& spaces);

private:
    std::vector<AgentProjection> compute(SpaceController& spaces);
    std::vector<AgentProjection> compute_server_agents(
        SpaceController& spaces);

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
    std::vector<AgentProjection> server_agents_;
    mutable std::unordered_map<std::string, uint64_t>
        server_attention_acknowledged_;
    bool server_agents_authoritative_ = false;
    std::vector<AgentProjection> cached_agents_;
    bool cache_valid_ = false;
};

} // namespace draxul
