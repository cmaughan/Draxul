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
    std::vector<AgentProjection> query(const SpaceController& spaces);
    bool focus(SpaceController& spaces, std::string_view instance_id) const;
    bool focus_by_index(SpaceController& spaces, int one_based_index);

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

    std::unordered_map<std::string, CachedSemanticState> semantic_state_;
};

} // namespace draxul
