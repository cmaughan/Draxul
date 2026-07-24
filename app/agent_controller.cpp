#include "agent_controller.h"

#include "space_controller.h"

#include <unordered_set>

namespace draxul
{

std::vector<AgentProjection> AgentController::query(const SpaceController& spaces)
{
    std::vector<AgentProjection> agents;
    std::unordered_set<std::string> live_instances;
    for (const auto& space : spaces.spaces())
    {
        for (const auto& tab : space->tab_controller.tabs())
        {
            const PaneManager& panes = tab->pane_manager;
            panes.tree().for_each_leaf([&](LeafId leaf, const PaneDescriptor&) {
                const AgentIdentity* identity = panes.agent_identity(leaf);
                if (!identity)
                    return;
                IHost* host = panes.host_for(leaf);
                const bool running = host && host->is_running();
                const std::optional<int> exit_code = host ? host->exit_code() : std::nullopt;
                const AgentLifecycle lifecycle = running
                    ? AgentLifecycle::Running
                    : (exit_code && *exit_code != 0 ? AgentLifecycle::Failed
                                                    : AgentLifecycle::Exited);
                const AgentRuntimeGeneration generation =
                    panes.agent_runtime_generation(leaf);
                const bool focused = space->id == spaces.active_space_id()
                    && tab->id == space->tab_controller.active_tab_id()
                    && leaf == panes.focused_leaf();

                live_instances.insert(identity->instance_id);
                auto& semantic = semantic_state_[identity->instance_id];
                if (semantic.runtime_generation != generation)
                {
                    semantic = {};
                    semantic.runtime_generation = generation;
                    semantic.lifecycle_transition_at =
                        panes.agent_runtime_started_at(leaf);
                }
                if (semantic.lifecycle != lifecycle)
                {
                    semantic.lifecycle = lifecycle;
                    semantic.lifecycle_transition_at =
                        std::chrono::steady_clock::now();
                }

                if (host)
                {
                    if (const auto observation =
                            host->capture_agent_observation(12, 8 * 1024))
                    {
                        const uint64_t cached_generation =
                            semantic.explanation.observation_generation;
                        const auto debounce = std::chrono::milliseconds(100);
                        const auto observation_time =
                            observation->captured_at
                                == std::chrono::steady_clock::time_point{}
                            ? std::chrono::steady_clock::now()
                            : observation->captured_at;
                        const bool output_stable = !observation->last_output_at
                            || !observation->process_running
                            || observation_time - *observation->last_output_at
                                >= debounce;
                        const bool rate_limit_elapsed =
                            semantic.explanation.evaluated_at
                                == std::chrono::steady_clock::time_point{}
                            || observation_time
                                    - semantic.explanation.evaluated_at
                                >= debounce;
                        const bool observation_changed =
                            semantic.explanation.evaluated_at
                                == std::chrono::steady_clock::time_point{}
                            || observation->output_generation > cached_generation;
                        if (observation_changed && output_stable
                            && rate_limit_elapsed)
                        {
                            const AgentStatus previous = semantic.explanation.status;
                            auto explanation =
                                evaluate_agent_observation(identity->kind, *observation);
                            if (explanation.status != previous)
                            {
                                semantic.last_transition_at = explanation.evaluated_at;
                                if (!focused
                                    && (explanation.status == AgentStatus::Blocked
                                        || explanation.status == AgentStatus::Done))
                                {
                                    semantic.attention = true;
                                }
                            }
                            semantic.explanation = std::move(explanation);
                        }
                    }
                }
                if (focused)
                    semantic.attention = false;

                agents.push_back({
                    .space_id = space->id,
                    .tab_id = tab->id,
                    .leaf_id = leaf,
                    .pane_id = panes.pane_id(leaf),
                    .identity = *identity,
                    .session_ref = panes.agent_session_ref(leaf)
                        ? std::optional(*panes.agent_session_ref(leaf))
                        : std::nullopt,
                    .lifecycle = lifecycle,
                    .generation = generation,
                    .runtime_started_at = panes.agent_runtime_started_at(leaf),
                    .lifecycle_transition_at = semantic.lifecycle_transition_at,
                    .exit_code = exit_code,
                    .status = semantic.explanation.status,
                    .status_authority = semantic.explanation.authority,
                    .status_explanation = semantic.explanation,
                    .attention = semantic.attention,
                    .last_status_transition_at = semantic.last_transition_at,
                    .running = running,
                    .focused = focused,
                });
            });
        }
    }
    std::erase_if(semantic_state_, [&live_instances](const auto& entry) {
        return !live_instances.contains(entry.first);
    });
    return agents;
}

bool AgentController::focus(
    SpaceController& spaces, std::string_view instance_id) const
{
    for (auto& space : spaces.spaces())
    {
        for (auto& tab : space->tab_controller.tabs())
        {
            PaneManager& panes = tab->pane_manager;
            bool found = false;
            LeafId target = kInvalidLeaf;
            panes.tree().for_each_leaf([&](LeafId leaf, const PaneDescriptor&) {
                if (found)
                    return;
                const AgentIdentity* identity = panes.agent_identity(leaf);
                if (identity && identity->instance_id == instance_id)
                {
                    found = true;
                    target = leaf;
                }
            });
            if (!found)
                continue;

            if (!spaces.activate_space(space->id))
                return false;
            if (!space->tab_controller.activate_tab(tab->id))
                return false;
            panes.set_focused(target);
            return true;
        }
    }
    return false;
}

bool AgentController::focus_by_index(
    SpaceController& spaces, int one_based_index)
{
    if (one_based_index <= 0)
        return false;
    const auto agents = query(spaces);
    const size_t index = static_cast<size_t>(one_based_index - 1);
    if (index >= agents.size())
        return false;
    return focus(spaces, agents[index].identity.instance_id);
}

} // namespace draxul
