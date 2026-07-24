#include "agent_controller.h"

#include "space_controller.h"

#include <sstream>
#include <unordered_set>

namespace draxul
{

std::vector<AgentProjection> AgentController::query(SpaceController& spaces)
{
    std::vector<AgentProjection> agents;
    std::unordered_set<std::string> live_instances;
    std::unordered_set<std::string> live_routes;
    const auto now = std::chrono::steady_clock::now();
    constexpr auto probe_interval = std::chrono::milliseconds(500);
    constexpr auto discovered_startup_grace = std::chrono::seconds(3);
    constexpr int removal_probe_count = 6;
    for (auto& space : spaces.spaces())
    {
        for (auto& tab : space->tab_controller.tabs())
        {
            PaneManager& panes = tab->pane_manager;
            panes.tree().for_each_leaf([&](LeafId leaf, const PaneDescriptor&) {
                IHost* host = panes.host_for(leaf);
                const AgentRuntimeGeneration generation =
                    panes.agent_runtime_generation(leaf);
                const std::string route = std::to_string(space->id) + ":"
                    + std::to_string(tab->id) + ":" + panes.pane_id(leaf);
                live_routes.insert(route);
                auto& discovery = discovery_state_[route];
                const AgentIdentity* identity = panes.agent_identity(leaf);
                if (discovery.runtime_generation != generation)
                {
                    const bool expired_manual_override =
                        discovery.manual_override && identity
                        && identity->origin == AgentIdentityOrigin::Discovered;
                    discovery = {};
                    discovery.runtime_generation = generation;
                    if (expired_manual_override)
                    {
                        panes.clear_agent_identity(leaf);
                        identity = nullptr;
                    }
                }

                const bool discovery_owned =
                    identity
                    && identity->origin == AgentIdentityOrigin::Discovered;
                const bool probe_due = discovery.last_probe_at
                        == std::chrono::steady_clock::time_point{}
                    || now - discovery.last_probe_at >= probe_interval;
                if ((!identity
                        || (discovery_owned && !discovery.manual_override))
                    && host && host->is_running() && probe_due)
                {
                    discovery.last_probe_at = now;
                    const auto observation =
                        host->capture_agent_process_observation();
                    const auto match = observation
                        ? discover_agent_process(*observation)
                        : std::nullopt;
                    if (match)
                    {
                        if (!identity && discovery.dismissed_kind == match->kind)
                        {
                            discovery.process_present = true;
                            discovery.dismissed_absent_probes = 0;
                            return;
                        }
                        if (!discovery.dismissed_kind.empty()
                            && discovery.dismissed_kind != match->kind)
                        {
                            discovery.dismissed_kind.clear();
                            discovery.dismissed_absent_probes = 0;
                        }
                        const bool new_occupant = !identity
                            || identity->origin != AgentIdentityOrigin::Discovered
                            || identity->kind != match->kind;
                        discovery.kind = match->kind;
                        discovery.evidence_category = match->evidence_category;
                        discovery.high_confidence = match->high_confidence;
                        discovery.failed_probes = 0;
                        discovery.process_present = true;
                        if (new_occupant)
                        {
                            discovery.detected_at = now;
                            std::ostringstream instance;
                            instance << "discovered-" << space->id << '-'
                                     << tab->id << '-' << panes.pane_id(leaf)
                                     << '-' << next_discovered_instance_++;
                            panes.set_agent_identity(leaf, {
                                .kind = match->kind,
                                .display_name = match->display_name,
                                .instance_id = instance.str(),
                                .origin = AgentIdentityOrigin::Discovered,
                            });
                            identity = panes.agent_identity(leaf);
                        }
                    }
                    else if (!identity && !discovery.dismissed_kind.empty())
                    {
                        if (++discovery.dismissed_absent_probes
                            >= removal_probe_count)
                        {
                            discovery.dismissed_kind.clear();
                            discovery.dismissed_absent_probes = 0;
                        }
                    }
                    else if (discovery_owned)
                    {
                        discovery.process_present = false;
                        if (++discovery.failed_probes >= removal_probe_count)
                        {
                            panes.clear_agent_identity(leaf);
                            identity = nullptr;
                            discovery = {};
                            discovery.runtime_generation = generation;
                        }
                    }
                }

                identity = panes.agent_identity(leaf);
                if (!identity)
                    return;
                const bool discovered =
                    identity->origin == AgentIdentityOrigin::Discovered;
                const bool host_running = host && host->is_running();
                const bool running = discovered
                    ? host_running
                        && (discovery.manual_override
                            || discovery.process_present)
                    : host_running;
                const std::optional<int> exit_code = host ? host->exit_code() : std::nullopt;
                const AgentLifecycle lifecycle = running
                    ? AgentLifecycle::Running
                    : (exit_code && *exit_code != 0 ? AgentLifecycle::Failed
                                                    : AgentLifecycle::Exited);
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

                const bool startup_grace_elapsed = !discovered
                    || discovery.detected_at
                            == std::chrono::steady_clock::time_point{}
                    || now - discovery.detected_at >= discovered_startup_grace;
                if (host && startup_grace_elapsed)
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
                    .identity_evidence_category = discovered
                        ? discovery.evidence_category
                        : "managed_launch",
                    .identity_high_confidence = discovered
                        ? discovery.high_confidence
                        : true,
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
    std::erase_if(discovery_state_, [&live_routes](const auto& entry) {
        return !live_routes.contains(entry.first);
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

bool AgentController::attach_focused(
    SpaceController& spaces, const AgentDefinition& definition)
{
    Space* space = spaces.find_active_space();
    if (!space || definition.kind.empty() || definition.display_name.empty())
        return false;
    Tab* tab = space->tab_controller.find_active_tab();
    if (!tab)
        return false;
    PaneManager& panes = tab->pane_manager;
    const LeafId leaf = panes.focused_leaf();
    IHost* host = panes.host_for(leaf);
    if (!host || !host->is_running())
        return false;

    const std::string route = std::to_string(space->id) + ":"
        + std::to_string(tab->id) + ":" + panes.pane_id(leaf);
    auto& discovery = discovery_state_[route];
    const bool correcting = panes.agent_identity(leaf) != nullptr;
    discovery = {};
    discovery.runtime_generation = panes.agent_runtime_generation(leaf);
    discovery.last_probe_at = std::chrono::steady_clock::now();
    discovery.detected_at = discovery.last_probe_at;
    discovery.kind = definition.kind;
    discovery.evidence_category =
        correcting ? "manual_correction" : "manual_attach";
    discovery.high_confidence = true;
    discovery.process_present = true;
    discovery.manual_override = true;

    std::ostringstream instance;
    instance << "attached-" << space->id << '-' << tab->id << '-'
             << panes.pane_id(leaf) << '-' << next_discovered_instance_++;
    panes.set_agent_identity(leaf, {
        .profile_id = definition.profile_id,
        .kind = definition.kind,
        .display_name = definition.display_name,
        .instance_id = instance.str(),
        .origin = AgentIdentityOrigin::Discovered,
    });
    return true;
}

bool AgentController::dismiss_focused(SpaceController& spaces)
{
    Space* space = spaces.find_active_space();
    if (!space)
        return false;
    Tab* tab = space->tab_controller.find_active_tab();
    if (!tab)
        return false;
    PaneManager& panes = tab->pane_manager;
    const LeafId leaf = panes.focused_leaf();
    const AgentIdentity* identity = panes.agent_identity(leaf);
    if (!identity)
        return false;

    const std::string route = std::to_string(space->id) + ":"
        + std::to_string(tab->id) + ":" + panes.pane_id(leaf);
    auto& discovery = discovery_state_[route];
    discovery.runtime_generation = panes.agent_runtime_generation(leaf);
    discovery.dismissed_kind = identity->kind;
    discovery.dismissed_absent_probes = 0;
    discovery.process_present = false;
    discovery.manual_override = false;
    return panes.clear_agent_identity(leaf);
}

} // namespace draxul
