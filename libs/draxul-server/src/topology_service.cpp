#include "topology_service.h"

#include <draxul/remote_terminal_protocol.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <nlohmann/json.hpp>

namespace draxul
{

namespace
{

TopologySpace* find_space(
    TopologySnapshot& snapshot, std::string_view space_id)
{
    const auto it = std::ranges::find(
        snapshot.spaces, space_id, &TopologySpace::space_id);
    return it == snapshot.spaces.end() ? nullptr : &*it;
}

TopologyTab* find_tab(
    TopologySpace& space, std::string_view tab_id)
{
    const auto it = std::ranges::find(
        space.tabs, tab_id, &TopologyTab::tab_id);
    return it == space.tabs.end() ? nullptr : &*it;
}

TopologyPane* find_pane(
    TopologyTab& tab, std::string_view pane_id)
{
    const auto it = std::ranges::find(
        tab.panes, pane_id, &TopologyPane::pane_id);
    return it == tab.panes.end() ? nullptr : &*it;
}

TopologyNode* find_node(
    TopologyTab& tab, std::string_view node_id)
{
    const auto it = std::ranges::find(
        tab.nodes, node_id, &TopologyNode::node_id);
    return it == tab.nodes.end() ? nullptr : &*it;
}

TopologyNode* find_leaf_for_pane(
    TopologyTab& tab, std::string_view pane_id)
{
    const auto it = std::ranges::find_if(tab.nodes,
        [pane_id](const TopologyNode& node) {
            return node.is_leaf && node.pane_id == pane_id;
        });
    return it == tab.nodes.end() ? nullptr : &*it;
}

TopologyNode* find_parent(
    TopologyTab& tab, std::string_view node_id)
{
    const auto it = std::ranges::find_if(tab.nodes,
        [node_id](const TopologyNode& node) {
            return !node.is_leaf
                && (node.first_node_id == node_id
                    || node.second_node_id == node_id);
        });
    return it == tab.nodes.end() ? nullptr : &*it;
}

bool valid_name(std::string_view value)
{
    return !value.empty() && value.size() <= kTopologyMaxTextBytes;
}

std::string command_key(const TopologyCommand& command)
{
    return command.client_id + '\n' + command.command_id;
}

uint64_t numeric_suffix(std::string_view value)
{
    const size_t separator = value.find_last_of('-');
    if (separator == std::string_view::npos
        || separator + 1 >= value.size())
    {
        return 0;
    }
    uint64_t parsed = 0;
    const char* begin = value.data() + separator + 1;
    const char* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    return result.ec == std::errc{} && result.ptr == end
        ? parsed
        : 0;
}

uint64_t next_serial_for(const TopologySnapshot& snapshot)
{
    uint64_t maximum = 0;
    for (const auto& space : snapshot.spaces)
    {
        maximum = std::max(maximum, numeric_suffix(space.space_id));
        for (const auto& tab : space.tabs)
        {
            maximum = std::max(maximum, numeric_suffix(tab.tab_id));
            for (const auto& node : tab.nodes)
                maximum = std::max(maximum, numeric_suffix(node.node_id));
            for (const auto& pane : tab.panes)
            {
                maximum = std::max(maximum, numeric_suffix(pane.pane_id));
                maximum = std::max(
                    maximum, numeric_suffix(pane.terminal_id));
            }
        }
    }
    return maximum + 1;
}

} // namespace

TopologyService::TopologyService(std::string session_id,
    TopologyServiceCallbacks callbacks)
    : callbacks_(std::move(callbacks))
{
    snapshot_.revision = 1;
    snapshot_.session_id = std::move(session_id);
    TopologySpace initial{
        .space_id = next_id("space"),
        .name = "Space 1",
        .tabs = {},
    };
    initial.tabs.push_back(make_initial_server_tab());
    snapshot_.spaces.push_back(std::move(initial));
}

TopologyService::TopologyService(TopologySnapshot snapshot,
    TopologyServiceCallbacks callbacks)
    : snapshot_(std::move(snapshot))
    , callbacks_(std::move(callbacks))
    , next_serial_(next_serial_for(snapshot_))
{
    // Older checkpoints stored this generated label as though it were a user
    // rename. Keep names custom-only so chrome can show the actual shell kind.
    for (auto& space : snapshot_.spaces)
    {
        for (auto& tab : space.tabs)
        {
            for (auto& pane : tab.panes)
            {
                if (pane.domain == TopologyPaneDomain::ServerTerminal
                    && pane.name == "Server Shell")
                {
                    pane.name.clear();
                }
            }
        }
    }
}

bool TopologyService::handles(std::string_view method) const
{
    return method == "topology.snapshot"
        || method == "topology.poll"
        || method == "topology.command";
}

ControlMethodResult TopologyService::handle(
    std::string_view method, const nlohmann::json& params)
{
    if (method == "topology.snapshot")
        return read_snapshot(params);
    if (method == "topology.poll")
        return poll(params);
    if (method == "topology.command")
        return command(params);
    return ControlMethodResult::error(
        "unknown_method", "Unknown topology method.");
}

ControlMethodResult TopologyService::report_agent_session(
    std::string_view pane_id,
    std::string_view agent_instance_id,
    const AgentSessionRef& session_ref)
{
    std::string validation_error;
    if (!validate_agent_session_ref(
            session_ref, &validation_error))
    {
        return ControlMethodResult::error(
            "invalid_session_ref", std::move(validation_error));
    }

    TopologyPane* target = nullptr;
    size_t matching_routes = 0;
    for (TopologySpace& space : snapshot_.spaces)
    {
        for (TopologyTab& tab : space.tabs)
        {
            for (TopologyPane& pane : tab.panes)
            {
                if (pane.pane_id != pane_id
                    || !pane.agent
                    || pane.agent->instance_id
                        != agent_instance_id)
                {
                    continue;
                }
                target = &pane;
                ++matching_routes;
            }
        }
    }
    if (matching_routes != 1 || !target
        || target->agent->kind != session_ref.agent_kind
        || target->agent->origin
            != AgentIdentityOrigin::Managed)
    {
        return ControlMethodResult::error(
            "routing_mismatch",
            "Agent routing identity does not match one managed pane.");
    }

    for (const TopologySpace& space : snapshot_.spaces)
    {
        for (const TopologyTab& tab : space.tabs)
        {
            for (const TopologyPane& pane : tab.panes)
            {
                const AgentSessionRef* existing
                    = pane.agent_session
                    ? &*pane.agent_session
                    : nullptr;
                if (&pane == target || !existing)
                    continue;
                if (existing->source == session_ref.source
                    && existing->agent_kind
                        == session_ref.agent_kind
                    && existing->kind == session_ref.kind
                    && existing->value == session_ref.value)
                {
                    return ControlMethodResult::error(
                        "duplicate_session_ref",
                        "Native agent session is already owned by another pane.");
                }
            }
        }
    }
    if (target->agent_session
        && session_ref.sequence
            <= target->agent_session->sequence)
    {
        return ControlMethodResult::error(
            "stale_report",
            "Native session report is stale or was rejected.");
    }

    target->agent_session = session_ref;
    ++snapshot_.revision;
    return ControlMethodResult::success({
        { "pane_id", target->pane_id },
        { "agent_instance_id",
            target->agent->instance_id },
        { "topology_revision", snapshot_.revision },
    });
}

ControlMethodResult TopologyService::launch_agent(
    std::string_view space_id,
    std::string_view tab_id,
    std::string_view target_pane_id,
    std::string_view name,
    const ManagedAgentTopologyLaunch& launch)
{
    TopologySpace* space = find_space(snapshot_, space_id);
    if (!space)
    {
        return ControlMethodResult::error(
            "space_not_found", "Topology Space was not found.");
    }
    TopologyTab* tab = find_tab(*space, tab_id);
    if (!tab)
    {
        return ControlMethodResult::error(
            "tab_not_found", "Topology tab was not found.");
    }
    TopologyNode* leaf
        = find_leaf_for_pane(*tab, target_pane_id);
    if (!leaf)
    {
        return ControlMethodResult::error(
            "pane_not_found", "Topology pane was not found.");
    }
    if (tab->panes.size() >= kTopologyMaxPanesPerTab)
    {
        return ControlMethodResult::error(
            "limit_reached", "Topology pane limit reached.");
    }
    if (!valid_name(name)
        || launch.identity.profile_id.empty()
        || launch.identity.kind.empty()
        || launch.identity.display_name.empty()
        || launch.identity.instance_id.empty()
        || launch.identity.origin
            != AgentIdentityOrigin::Managed
        || launch.working_directory.size()
            > kTopologyMaxTextBytes)
    {
        return ControlMethodResult::error(
            "invalid_agent",
            "Managed agent identity or launch route is invalid.");
    }
    if (!callbacks_.create_managed_agent_terminal)
    {
        return ControlMethodResult::error(
            "terminal_unavailable",
            "The server cannot allocate a managed agent terminal.");
    }

    const std::string pane_id = next_id("pane");
    std::string allocation_error;
    auto terminal_id
        = callbacks_.create_managed_agent_terminal(
            space->space_id, tab->tab_id, pane_id, name,
            launch, allocation_error);
    if (!terminal_id)
    {
        return ControlMethodResult::error(
            "agent_start_failed",
            allocation_error.empty()
                ? "The server could not start the managed agent."
                : std::move(allocation_error));
    }

    const std::string old_pane_id = leaf->pane_id;
    const std::string first_node_id = next_id("node");
    const std::string second_node_id = next_id("node");
    leaf->is_leaf = false;
    leaf->pane_id.clear();
    leaf->direction = TopologySplitDirection::Vertical;
    leaf->ratio = 0.5f;
    leaf->first_node_id = first_node_id;
    leaf->second_node_id = second_node_id;
    tab->nodes.push_back({
        .node_id = first_node_id,
        .is_leaf = true,
        .pane_id = old_pane_id,
    });
    tab->nodes.push_back({
        .node_id = second_node_id,
        .is_leaf = true,
        .pane_id = pane_id,
    });
    tab->panes.push_back({
        .pane_id = pane_id,
        .name = std::string(name),
        .domain = TopologyPaneDomain::ServerTerminal,
        .terminal_id = std::move(*terminal_id),
        .server_working_directory
        = launch.working_directory,
        .agent = launch.identity,
        .restore_policy = launch.restore_policy,
    });
    ++snapshot_.revision;
    return ControlMethodResult::success({
        { "space_id", space->space_id },
        { "tab_id", tab->tab_id },
        { "pane_id", pane_id },
        { "terminal_id", tab->panes.back().terminal_id },
        { "instance_id", launch.identity.instance_id },
        { "topology_revision", snapshot_.revision },
    });
}

ControlMethodResult TopologyService::close_exited_terminal(
    std::string_view terminal_id)
{
    if (terminal_id.empty())
    {
        return ControlMethodResult::error(
            "invalid_terminal",
            "Exited terminal identity is required.");
    }

    for (const TopologySpace& space : snapshot_.spaces)
    {
        for (const TopologyTab& tab : space.tabs)
        {
            const auto pane = std::ranges::find(
                tab.panes, terminal_id,
                &TopologyPane::terminal_id);
            if (pane == tab.panes.end())
                continue;

            TopologyCommand command{
                .kind = TopologyCommandKind::ClosePane,
                .space_id = space.space_id,
                .tab_id = tab.tab_id,
                .pane_id = pane->pane_id,
            };
            if (tab.panes.size() <= 1)
            {
                if (space.tabs.size() > 1)
                {
                    command.kind
                        = TopologyCommandKind::CloseTab;
                }
                else if (snapshot_.spaces.size() > 1)
                {
                    command.kind
                        = TopologyCommandKind::CloseSpace;
                }
                else
                {
                    return ControlMethodResult::error(
                        "last_pane",
                        "The final server pane cannot be removed yet.");
                }
            }

            std::string created_id;
            std::string error_code;
            std::string error;
            if (!apply(command, created_id,
                    error_code, error))
            {
                return ControlMethodResult::error(
                    std::move(error_code), std::move(error));
            }
            ++snapshot_.revision;
            return ControlMethodResult::success({
                { "terminal_id", terminal_id },
                { "topology_revision", snapshot_.revision },
            });
        }
    }
    return ControlMethodResult::error(
        "terminal_not_found",
        "Exited terminal is not present in shared topology.");
}

ControlMethodResult TopologyService::read_snapshot(
    const nlohmann::json&) const
{
    return ControlMethodResult::success(
        topology_snapshot_to_json(snapshot_));
}

ControlMethodResult TopologyService::poll(
    const nlohmann::json& params) const
{
    if (!params.is_object()
        || !params.contains("after_revision")
        || !params["after_revision"].is_number_unsigned())
    {
        return ControlMethodResult::error(
            "invalid_revision",
            "Topology poll requires an unsigned after_revision.");
    }
    const uint64_t after
        = params["after_revision"].get<uint64_t>();
    if (after > snapshot_.revision)
    {
        return ControlMethodResult::error(
            "stale_topology_revision",
            "The client topology revision is ahead of the server.");
    }
    if (after == snapshot_.revision)
    {
        return ControlMethodResult::success({
            { "changed", false },
            { "revision", snapshot_.revision },
        });
    }
    return ControlMethodResult::success({
        { "changed", true },
        { "revision", snapshot_.revision },
        { "snapshot", topology_snapshot_to_json(snapshot_) },
    });
}

ControlMethodResult TopologyService::command(
    const nlohmann::json& params)
{
    std::string parse_error;
    auto parsed = topology_command_from_json(params, parse_error);
    if (!parsed)
    {
        return ControlMethodResult::error(
            "invalid_command", std::move(parse_error));
    }
    const std::string key = command_key(*parsed);
    if (const auto completed = completed_.find(key);
        completed != completed_.end())
    {
        TopologyCommandResult duplicate{
            .applied = true,
            .duplicate = true,
            .created_id = completed->second,
            .snapshot = snapshot_,
        };
        return ControlMethodResult::success(
            topology_command_result_to_json(duplicate));
    }
    if (parsed->expected_revision != snapshot_.revision)
    {
        return ControlMethodResult::error(
            "revision_conflict",
            "Topology revision changed; refresh and retry.");
    }

    std::string created_id;
    std::string error_code;
    std::string error;
    if (!apply(*parsed, created_id, error_code, error))
        return ControlMethodResult::error(
            std::move(error_code), std::move(error));

    ++snapshot_.revision;
    TopologyCommandResult result{
        .applied = true,
        .created_id = std::move(created_id),
        .snapshot = snapshot_,
    };
    remember(key, result.created_id);
    return ControlMethodResult::success(
        topology_command_result_to_json(result));
}

bool TopologyService::apply(const TopologyCommand& command,
    std::string& created_id, std::string& error_code,
    std::string& error)
{
    auto reject = [&](std::string code, std::string message) {
        error_code = std::move(code);
        error = std::move(message);
        return false;
    };

    if (command.kind == TopologyCommandKind::CreateSpace)
    {
        if (snapshot_.spaces.size() >= kTopologyMaxSpaces)
            return reject("limit_reached", "Topology Space limit reached.");
        if (!valid_name(command.name))
            return reject("invalid_name", "Space name is required.");
        TopologySpace space{
            .space_id = next_id("space"),
            .name = command.name,
            .root_directory = command.root_directory,
            .tabs = {},
        };
        space.tabs.push_back(make_client_local_tab("Tab 1"));
        TopologyPane& pane = space.tabs.front().panes.front();
        if (command.pane_domain
            == TopologyPaneDomain::ServerTerminal)
        {
            if (!callbacks_.create_server_terminal)
                return reject("terminal_unavailable",
                    "The server cannot allocate another terminal.");
            std::string allocation_error;
            const auto terminal_id
                = callbacks_.create_server_terminal(
                    pane.pane_id, pane.name, allocation_error);
            if (!terminal_id)
            {
                return reject("terminal_start_failed",
                    allocation_error.empty()
                        ? "The server could not allocate a terminal."
                        : std::move(allocation_error));
            }
            pane.domain = TopologyPaneDomain::ServerTerminal;
            pane.terminal_id = *terminal_id;
            pane.client_host_kind.clear();
            pane.client_working_directory.clear();
            pane.client_source_path.clear();
            pane.companion_owner_pane_id.clear();
        }
        else if (!command.client_host_kind.empty())
        {
            pane.client_host_kind = command.client_host_kind;
            pane.client_working_directory
                = command.client_working_directory;
            pane.client_source_path
                = command.client_source_path;
        }
        created_id = space.space_id;
        snapshot_.spaces.push_back(std::move(space));
        return true;
    }

    TopologySpace* space = find_space(snapshot_, command.space_id);
    if (!space)
        return reject("space_not_found", "Topology Space was not found.");
    if (command.kind == TopologyCommandKind::RenameSpace)
    {
        if (!valid_name(command.name))
            return reject("invalid_name", "Space name is required.");
        space->name = command.name;
        return true;
    }
    if (command.kind == TopologyCommandKind::CloseSpace)
    {
        if (snapshot_.spaces.size() <= 1)
            return reject("last_space", "Cannot close the final Space.");
        for (const TopologyTab& tab : space->tabs)
        {
            for (const TopologyPane& pane : tab.panes)
            {
                if (pane.domain == TopologyPaneDomain::ServerTerminal
                    && callbacks_.destroy_server_terminal)
                {
                    callbacks_.destroy_server_terminal(
                        pane.terminal_id);
                }
            }
        }
        std::erase_if(snapshot_.spaces,
            [&](const TopologySpace& candidate) {
                return candidate.space_id == command.space_id;
            });
        return true;
    }
    if (command.kind == TopologyCommandKind::CreateTab)
    {
        if (space->tabs.size() >= kTopologyMaxTabsPerSpace)
            return reject("limit_reached", "Topology tab limit reached.");
        TopologyTab tab = make_client_local_tab(
            valid_name(command.name) ? command.name : "Tab");
        TopologyPane& pane = tab.panes.front();
        if (command.pane_domain
            == TopologyPaneDomain::ServerTerminal)
        {
            if (!callbacks_.create_server_terminal)
                return reject("terminal_unavailable",
                    "The server cannot allocate another terminal.");
            std::string allocation_error;
            const auto terminal_id
                = callbacks_.create_server_terminal(
                    pane.pane_id, pane.name, allocation_error);
            if (!terminal_id)
            {
                return reject("terminal_start_failed",
                    allocation_error.empty()
                        ? "The server could not allocate a terminal."
                        : std::move(allocation_error));
            }
            pane.domain = TopologyPaneDomain::ServerTerminal;
            pane.terminal_id = *terminal_id;
            pane.client_host_kind.clear();
            pane.client_working_directory.clear();
            pane.client_source_path.clear();
            pane.companion_owner_pane_id.clear();
        }
        else if (!command.client_host_kind.empty())
        {
            pane.client_host_kind = command.client_host_kind;
            pane.client_working_directory
                = command.client_working_directory;
            pane.client_source_path
                = command.client_source_path;
        }
        created_id = tab.tab_id;
        space->tabs.push_back(std::move(tab));
        return true;
    }

    TopologyTab* tab = find_tab(*space, command.tab_id);
    if (!tab)
        return reject("tab_not_found", "Topology tab was not found.");
    if (command.kind == TopologyCommandKind::RenameTab)
    {
        if (!valid_name(command.name))
            return reject("invalid_name", "Tab name is required.");
        tab->name = command.name;
        tab->name_user_set = true;
        return true;
    }
    if (command.kind == TopologyCommandKind::CloseTab)
    {
        if (space->tabs.size() <= 1)
            return reject("last_tab", "Cannot close the final tab.");
        for (const TopologyPane& pane : tab->panes)
        {
            if (pane.domain == TopologyPaneDomain::ServerTerminal
                && callbacks_.destroy_server_terminal)
            {
                callbacks_.destroy_server_terminal(
                    pane.terminal_id);
            }
        }
        std::erase_if(space->tabs,
            [&](const TopologyTab& candidate) {
                return candidate.tab_id == command.tab_id;
            });
        return true;
    }
    if (command.kind == TopologyCommandKind::MoveTab)
    {
        if (command.move_delta != -1 && command.move_delta != 1)
            return reject("invalid_move", "Tab move must be -1 or 1.");
        const auto current = std::find_if(
            space->tabs.begin(), space->tabs.end(),
            [&](const TopologyTab& candidate) {
                return candidate.tab_id == command.tab_id;
            });
        if (current == space->tabs.end())
            return reject("tab_not_found", "Topology tab was not found.");
        if (space->tabs.size() <= 1)
            return true;
        const auto index = static_cast<size_t>(
            std::distance(space->tabs.begin(), current));
        const auto count = static_cast<int>(space->tabs.size());
        const auto target = static_cast<size_t>(
            (static_cast<int>(index) + command.move_delta + count) % count);
        std::swap(space->tabs[index], space->tabs[target]);
        return true;
    }
    if (command.kind == TopologyCommandKind::RenamePane)
    {
        TopologyPane* pane = find_pane(*tab, command.pane_id);
        if (!pane)
            return reject("pane_not_found", "Topology pane was not found.");
        pane->name = command.name;
        return true;
    }
    if (command.kind == TopologyCommandKind::UpdateClientPane)
    {
        TopologyPane* pane = find_pane(*tab, command.pane_id);
        if (!pane)
            return reject("pane_not_found", "Topology pane was not found.");
        if (pane->domain != TopologyPaneDomain::ClientLocal)
        {
            return reject("server_terminal_pane",
                "Only client-local pane launch data can be updated.");
        }
        if (!command.client_host_kind.empty()
            && command.client_host_kind != pane->client_host_kind)
        {
            return reject("host_kind_mismatch",
                "Client-local pane host kind cannot be changed in place.");
        }
        pane->client_working_directory
            = command.client_working_directory;
        pane->client_source_path = command.client_source_path;
        return true;
    }
    if (command.kind == TopologyCommandKind::SwapPane)
    {
        if (command.pane_id == command.target_pane_id)
            return reject("invalid_swap", "Pane swap requires two panes.");
        TopologyNode* first
            = find_leaf_for_pane(*tab, command.pane_id);
        TopologyNode* second
            = find_leaf_for_pane(*tab, command.target_pane_id);
        if (!first || !second)
            return reject("pane_not_found", "Topology pane was not found.");
        std::swap(first->pane_id, second->pane_id);
        return true;
    }
    if (command.kind == TopologyCommandKind::RestartPane)
    {
        const TopologyPane* pane
            = find_pane(*tab, command.pane_id);
        if (!pane)
            return reject("pane_not_found", "Topology pane was not found.");
        if (pane->domain != TopologyPaneDomain::ServerTerminal)
        {
            return reject("client_local_pane",
                "Client-local panes restart in their owning UI.");
        }
        if (!callbacks_.restart_server_terminal)
        {
            return reject("terminal_unavailable",
                "The server cannot restart this terminal.");
        }
        std::string restart_error;
        if (!callbacks_.restart_server_terminal(
                pane->terminal_id, restart_error))
        {
            return reject("terminal_restart_failed",
                restart_error.empty()
                    ? "The server terminal could not be restarted."
                    : std::move(restart_error));
        }
        return true;
    }
    if (command.kind == TopologyCommandKind::SetSplitRatio)
    {
        TopologyNode* node = find_node(*tab, command.node_id);
        if (!node || node->is_leaf)
            return reject("split_not_found", "Topology split was not found.");
        if (!std::isfinite(command.ratio)
            || command.ratio < 0.1f || command.ratio > 0.9f)
        {
            return reject(
                "invalid_ratio", "Split ratio must be between 0.1 and 0.9.");
        }
        node->ratio = command.ratio;
        return true;
    }
    if (command.kind == TopologyCommandKind::EqualizeSplits)
    {
        for (TopologyNode& node : tab->nodes)
        {
            if (!node.is_leaf)
                node.ratio = 0.5f;
        }
        return true;
    }
    if (command.kind == TopologyCommandKind::SplitPane)
    {
        if (tab->panes.size() >= kTopologyMaxPanesPerTab)
            return reject("limit_reached", "Topology pane limit reached.");
        if (!std::isfinite(command.ratio)
            || command.ratio < 0.1f
            || command.ratio > 0.9f)
        {
            return reject(
                "invalid_ratio", "Split ratio must be between 0.1 and 0.9.");
        }
        TopologyNode* leaf = find_leaf_for_pane(*tab, command.pane_id);
        if (!leaf)
            return reject("pane_not_found", "Topology pane was not found.");
        const std::string pane_id = next_id("pane");
        TopologyPane pane{
            .pane_id = pane_id,
            .name = command.name,
            .domain = command.pane_domain,
            .terminal_id = command.terminal_id,
            .client_host_kind = command.client_host_kind,
            .client_working_directory
                = command.client_working_directory,
            .client_source_path = command.client_source_path,
            .companion_owner_pane_id
                = command.companion_owner_pane_id,
        };
        if (pane.domain == TopologyPaneDomain::ServerTerminal)
        {
            if (!pane.terminal_id.empty())
            {
                return reject("invalid_terminal",
                    "Clients cannot assign an existing server terminal.");
            }
            if (!callbacks_.create_server_terminal)
            {
                return reject("terminal_unavailable",
                    "The server cannot allocate another terminal.");
            }
            std::string allocation_error;
            const auto terminal_id
                = callbacks_.create_server_terminal(
                    pane_id, pane.name, allocation_error);
            if (!terminal_id)
            {
                return reject("terminal_start_failed",
                    allocation_error.empty()
                        ? "The server could not allocate a terminal."
                        : std::move(allocation_error));
            }
            pane.terminal_id = *terminal_id;
            pane.client_host_kind.clear();
            pane.client_working_directory.clear();
            pane.client_source_path.clear();
            pane.companion_owner_pane_id.clear();
        }
        else
        {
            pane.terminal_id.clear();
            if (pane.client_host_kind.empty())
                pane.client_host_kind = "platform_default";
            if (!pane.companion_owner_pane_id.empty()
                && !find_pane(*tab,
                    pane.companion_owner_pane_id))
            {
                return reject("companion_owner_not_found",
                    "Companion pane owner was not found.");
            }
        }

        const std::string old_pane_id = leaf->pane_id;
        const std::string first_node_id = next_id("node");
        const std::string second_node_id = next_id("node");
        leaf->is_leaf = false;
        leaf->pane_id.clear();
        leaf->direction = command.direction;
        leaf->ratio = command.ratio;
        leaf->first_node_id = first_node_id;
        leaf->second_node_id = second_node_id;
        tab->nodes.push_back({
            .node_id = first_node_id,
            .is_leaf = true,
            .pane_id = old_pane_id,
        });
        tab->nodes.push_back({
            .node_id = second_node_id,
            .is_leaf = true,
            .pane_id = pane.pane_id,
        });
        created_id = pane_id;
        tab->panes.push_back(std::move(pane));
        return true;
    }
    if (command.kind == TopologyCommandKind::ClosePane)
    {
        if (tab->panes.size() <= 1)
            return reject("last_pane", "Cannot close the final pane.");
        TopologyNode* leaf = find_leaf_for_pane(*tab, command.pane_id);
        if (!leaf)
            return reject("pane_not_found", "Topology pane was not found.");
        const TopologyPane* closing_pane
            = find_pane(*tab, command.pane_id);
        if (!closing_pane)
            return reject("pane_not_found", "Topology pane was not found.");
        const std::string closing_terminal_id
            = closing_pane->domain
                == TopologyPaneDomain::ServerTerminal
            ? closing_pane->terminal_id
            : std::string{};
        TopologyNode* parent = find_parent(*tab, leaf->node_id);
        if (!parent)
            return reject("invalid_topology", "Topology pane has no parent.");
        const std::string leaf_node_id = leaf->node_id;
        const std::string sibling_id
            = parent->first_node_id == leaf_node_id
            ? parent->second_node_id
            : parent->first_node_id;
        TopologyNode* sibling = find_node(*tab, sibling_id);
        if (!sibling)
            return reject("invalid_topology", "Topology sibling was not found.");
        const std::string parent_id = parent->node_id;
        TopologyNode replacement = *sibling;
        replacement.node_id = parent_id;
        *parent = std::move(replacement);
        std::erase_if(tab->nodes, [&](const TopologyNode& node) {
            return node.node_id == leaf_node_id
                || node.node_id == sibling_id;
        });
        std::erase_if(tab->panes, [&](const TopologyPane& pane) {
            return pane.pane_id == command.pane_id;
        });
        if (!closing_terminal_id.empty()
            && callbacks_.destroy_server_terminal)
        {
            callbacks_.destroy_server_terminal(
                closing_terminal_id);
        }
        return true;
    }
    return reject(
        "unsupported_command", "Unsupported topology command.");
}

std::string TopologyService::next_id(std::string_view prefix)
{
    return std::string(prefix) + "-" + std::to_string(next_serial_++);
}

TopologyTab TopologyService::make_client_local_tab(std::string name)
{
    TopologyPane pane{
        .pane_id = next_id("pane"),
        .name = {},
        .domain = TopologyPaneDomain::ClientLocal,
        .client_host_kind = "platform_default",
    };
    TopologyNode node{
        .node_id = next_id("node"),
        .is_leaf = true,
        .pane_id = pane.pane_id,
    };
    return {
        .tab_id = next_id("tab"),
        .name = std::move(name),
        .name_user_set = false,
        .root_node_id = node.node_id,
        .nodes = { std::move(node) },
        .panes = { std::move(pane) },
    };
}

TopologyTab TopologyService::make_initial_server_tab()
{
    TopologyPane pane{
        .pane_id = std::string(kServerShellPaneId),
        .name = {},
        .domain = TopologyPaneDomain::ServerTerminal,
        .terminal_id = std::string(kServerShellTerminalId),
    };
    TopologyNode node{
        .node_id = next_id("node"),
        .is_leaf = true,
        .pane_id = pane.pane_id,
    };
    return {
        .tab_id = next_id("tab"),
        .name = "Shell",
        .name_user_set = false,
        .root_node_id = node.node_id,
        .nodes = { std::move(node) },
        .panes = { std::move(pane) },
    };
}

void TopologyService::remember(
    std::string key, std::string created_id)
{
    if (completed_order_.size() >= kTopologyCompletedCommandLimit)
    {
        completed_.erase(completed_order_.front());
        completed_order_.pop_front();
    }
    completed_order_.push_back(key);
    completed_.emplace(
        std::move(key), std::move(created_id));
}

} // namespace draxul
