#include "agent_controller.h"

#include "space_controller.h"

namespace draxul
{

std::vector<AgentProjection> AgentController::query(const SpaceController& spaces) const
{
    std::vector<AgentProjection> agents;
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
                agents.push_back({
                    .space_id = space->id,
                    .tab_id = tab->id,
                    .leaf_id = leaf,
                    .pane_id = panes.pane_id(leaf),
                    .identity = *identity,
                    .lifecycle = lifecycle,
                    .generation = panes.agent_runtime_generation(leaf),
                    .exit_code = exit_code,
                    .running = running,
                    .focused = space->id == spaces.active_space_id()
                        && tab->id == space->tab_controller.active_tab_id()
                        && leaf == panes.focused_leaf(),
                });
            });
        }
    }
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
    SpaceController& spaces, int one_based_index) const
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
