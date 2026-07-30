#include "topology_mutation_route.h"

#include <draxul/host_kind.h>

#include <algorithm>
#include <utility>

namespace draxul
{

TopologyMutationResult TopologyMutationResult::applied()
{
    return { .state = TopologyMutationState::Applied };
}

TopologyMutationResult TopologyMutationResult::enqueued()
{
    return { .state = TopologyMutationState::Enqueued };
}

TopologyMutationResult TopologyMutationResult::rejected(
    std::string error)
{
    return {
        .state = TopologyMutationState::Rejected,
        .error = std::move(error),
    };
}

LocalTopologyMutationRoute::LocalTopologyMutationRoute(
    Handler handler)
    : handler_(std::move(handler))
{
}

TopologyMutationRouteKind
LocalTopologyMutationRoute::route_kind() const noexcept
{
    return TopologyMutationRouteKind::Local;
}

TopologyMutationResult LocalTopologyMutationRoute::mutate(
    const TopologyMutation& mutation)
{
    if (!handler_)
    {
        return TopologyMutationResult::rejected(
            "Local topology mutation route is unavailable.");
    }
    return handler_(mutation);
}

ServerTopologyMutationRoute::ServerTopologyMutationRoute(
    Deps deps)
    : deps_(std::move(deps))
{
}

TopologyMutationRouteKind
ServerTopologyMutationRoute::route_kind() const noexcept
{
    return TopologyMutationRouteKind::ServerBacked;
}

TopologyMutationResult ServerTopologyMutationRoute::enqueue(
    TopologyCommand command) const
{
    if (!deps_.enqueue)
    {
        return TopologyMutationResult::rejected(
            "Shared topology mutation route is unavailable.");
    }
    std::string error;
    if (!deps_.enqueue(std::move(command), error))
    {
        return TopologyMutationResult::rejected(
            error.empty()
                ? "Shared topology mutation was rejected."
                : std::move(error));
    }
    return TopologyMutationResult::enqueued();
}

TopologyMutationResult
ServerTopologyMutationRoute::reject_unresolved(
    std::string_view subject) const
{
    return TopologyMutationResult::rejected(
        "Shared " + std::string(subject)
        + " could not be resolved.");
}

TopologyMutationResult ServerTopologyMutationRoute::mutate(
    const TopologyMutation& mutation)
{
    const auto resolve_space = [&]() {
        return deps_.resolve_space
            ? deps_.resolve_space(mutation.space_id)
            : std::optional<std::string>{};
    };
    const auto resolve_tab = [&]() {
        return deps_.resolve_tab
            ? deps_.resolve_tab(
                  mutation.space_id, mutation.tab_id)
            : std::optional<std::string>{};
    };
    const auto resolve_pane = [&](LeafId leaf) {
        return deps_.resolve_pane
            ? deps_.resolve_pane(
                  mutation.space_id, mutation.tab_id, leaf)
            : std::optional<std::string>{};
    };

    TopologyCommand command;
    switch (mutation.kind)
    {
    case TopologyMutationKind::CreateSpace:
        command = {
            .kind = TopologyCommandKind::CreateSpace,
            .name = mutation.name,
            .root_directory
                = mutation.root_directory.string(),
            .pane_domain
                = TopologyPaneDomain::ServerTerminal,
        };
        break;
    case TopologyMutationKind::RenameSpace:
    case TopologyMutationKind::CloseSpace:
    {
        const auto space = resolve_space();
        if (!space)
            return reject_unresolved("Space");
        command.kind
            = mutation.kind
                == TopologyMutationKind::RenameSpace
            ? TopologyCommandKind::RenameSpace
            : TopologyCommandKind::CloseSpace;
        command.space_id = *space;
        command.name = mutation.name;
        break;
    }
    case TopologyMutationKind::CreateTab:
    {
        const auto space = resolve_space();
        if (!space)
            return reject_unresolved("Space");
        const HostKind kind = mutation.host_kind.value_or(
            deps_.platform_default_host_kind);
        const bool server_terminal
            = !mutation.host_kind
            || is_server_owned_shell_host(kind);
        command = {
            .kind = TopologyCommandKind::CreateTab,
            .space_id = *space,
            .name = mutation.name.empty()
                ? "Tab"
                : mutation.name,
            .pane_domain = server_terminal
                ? TopologyPaneDomain::ServerTerminal
                : TopologyPaneDomain::ClientLocal,
            .client_host_kind = server_terminal
                ? std::string{}
                : std::string(to_string(kind)),
        };
        break;
    }
    case TopologyMutationKind::RenameTab:
    case TopologyMutationKind::CloseTab:
    case TopologyMutationKind::MoveTab:
    {
        const auto space = resolve_space();
        const auto tab = resolve_tab();
        if (!space || !tab)
            return reject_unresolved("tab");
        command.kind
            = mutation.kind
                == TopologyMutationKind::RenameTab
            ? TopologyCommandKind::RenameTab
            : mutation.kind
                    == TopologyMutationKind::CloseTab
            ? TopologyCommandKind::CloseTab
            : TopologyCommandKind::MoveTab;
        command.space_id = *space;
        command.tab_id = *tab;
        command.name = mutation.name;
        command.move_delta = mutation.move_delta;
        break;
    }
    case TopologyMutationKind::SplitPane:
    case TopologyMutationKind::DuplicatePane:
    {
        const auto space = resolve_space();
        const auto tab = resolve_tab();
        const auto pane = resolve_pane(mutation.pane_id);
        if (!space || !tab || !pane)
            return reject_unresolved("focused pane");
        const std::optional<HostKind> requested_kind
            = mutation.kind
                    == TopologyMutationKind::DuplicatePane
            ? std::optional<HostKind>(
                  deps_.platform_default_host_kind)
            : mutation.host_kind;
        const HostKind kind = requested_kind.value_or(
            deps_.platform_default_host_kind);
        const bool server_terminal
            = !requested_kind
            || is_server_owned_shell_host(kind);
        command = {
            .kind = TopologyCommandKind::SplitPane,
            .space_id = *space,
            .tab_id = *tab,
            .pane_id = *pane,
            .direction
                = mutation.kind
                        == TopologyMutationKind::DuplicatePane
                ? TopologySplitDirection::Vertical
                : mutation.direction,
            .pane_domain = server_terminal
                ? TopologyPaneDomain::ServerTerminal
                : TopologyPaneDomain::ClientLocal,
            .client_host_kind = server_terminal
                ? std::string{}
                : std::string(to_string(kind)),
        };
        break;
    }
    case TopologyMutationKind::ClosePane:
    case TopologyMutationKind::RenamePane:
    case TopologyMutationKind::SwapPane:
    case TopologyMutationKind::RestartPane:
    {
        const auto space = resolve_space();
        const auto tab = resolve_tab();
        const auto pane = resolve_pane(mutation.pane_id);
        if (!space || !tab || !pane)
            return reject_unresolved("focused pane");

        if (mutation.kind
                == TopologyMutationKind::RestartPane
            && deps_.resolve_pane_domain)
        {
            const auto domain = deps_.resolve_pane_domain(
                mutation.space_id, mutation.tab_id,
                mutation.pane_id);
            if (!domain)
                return reject_unresolved("focused pane");
            if (*domain == TopologyPaneDomain::ClientLocal)
            {
                if (!deps_.apply_client_local)
                {
                    return TopologyMutationResult::rejected(
                        "Client-local pane mutation route is unavailable.");
                }
                return deps_.apply_client_local(mutation);
            }
        }

        command.kind
            = mutation.kind
                == TopologyMutationKind::ClosePane
            ? TopologyCommandKind::ClosePane
            : mutation.kind
                    == TopologyMutationKind::RenamePane
            ? TopologyCommandKind::RenamePane
            : mutation.kind
                    == TopologyMutationKind::SwapPane
            ? TopologyCommandKind::SwapPane
            : TopologyCommandKind::RestartPane;
        command.space_id = *space;
        command.tab_id = *tab;
        command.pane_id = *pane;
        command.name = mutation.name;
        if (mutation.kind == TopologyMutationKind::SwapPane)
        {
            const auto target = resolve_pane(
                mutation.target_pane_id);
            if (!target)
                return reject_unresolved("pane reorder target");
            command.target_pane_id = *target;
        }
        break;
    }
    case TopologyMutationKind::SetSplitRatio:
    {
        const auto space = resolve_space();
        const auto tab = resolve_tab();
        if (!space || !tab || !deps_.resolve_divider)
            return reject_unresolved("split");
        const auto node = deps_.resolve_divider(
            *tab, mutation.divider_id);
        if (!node)
            return reject_unresolved("split");
        command = {
            .kind = TopologyCommandKind::SetSplitRatio,
            .space_id = *space,
            .tab_id = *tab,
            .node_id = *node,
            .ratio = std::clamp(
                mutation.ratio, 0.1f, 0.9f),
        };
        break;
    }
    case TopologyMutationKind::EqualizeSplits:
    {
        const auto space = resolve_space();
        const auto tab = resolve_tab();
        if (!space || !tab)
            return reject_unresolved("tab");
        command = {
            .kind = TopologyCommandKind::EqualizeSplits,
            .space_id = *space,
            .tab_id = *tab,
        };
        break;
    }
    }
    return enqueue(std::move(command));
}

std::unique_ptr<ITopologyMutationRoute>
make_topology_mutation_route(bool server_backed,
    LocalTopologyMutationRoute::Handler local_handler,
    ServerTopologyMutationRoute::Deps server_deps)
{
    if (server_backed)
    {
        return std::make_unique<
            ServerTopologyMutationRoute>(
            std::move(server_deps));
    }
    return std::make_unique<
        LocalTopologyMutationRoute>(
        std::move(local_handler));
}

} // namespace draxul
