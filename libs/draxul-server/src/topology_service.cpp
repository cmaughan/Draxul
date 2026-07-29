#include "topology_service.h"

#include <draxul/remote_terminal_protocol.h>

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace draxul
{

namespace
{

constexpr size_t kCompletedCommandLimit = 2048;

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
    if (after >= snapshot_.revision)
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
        TopologyCommandResult duplicate = completed->second;
        duplicate.duplicate = true;
        duplicate.snapshot = snapshot_;
        return ControlMethodResult::success(
            topology_command_result_to_json(duplicate));
    }
    if (parsed->expected_revision != snapshot_.revision)
    {
        return ControlMethodResult::error(
            "revision_conflict",
            "Topology revision changed; refresh and retry.");
    }

    std::string error_code;
    std::string error;
    if (!apply(*parsed, error_code, error))
        return ControlMethodResult::error(
            std::move(error_code), std::move(error));

    ++snapshot_.revision;
    TopologyCommandResult result{
        .applied = true,
        .snapshot = snapshot_,
    };
    remember(key, result);
    return ControlMethodResult::success(
        topology_command_result_to_json(result));
}

bool TopologyService::apply(const TopologyCommand& command,
    std::string& error_code, std::string& error)
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
        }
        else if (!command.client_host_kind.empty())
        {
            pane.client_host_kind = command.client_host_kind;
        }
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
        }
        else if (!command.client_host_kind.empty())
            tab.panes.front().client_host_kind = command.client_host_kind;
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
        }
        else
        {
            pane.terminal_id.clear();
            if (pane.client_host_kind.empty())
                pane.client_host_kind = "platform_default";
        }

        const std::string old_pane_id = leaf->pane_id;
        const std::string first_node_id = next_id("node");
        const std::string second_node_id = next_id("node");
        leaf->is_leaf = false;
        leaf->pane_id.clear();
        leaf->direction = command.direction;
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
            .pane_id = pane.pane_id,
        });
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
        .root_node_id = node.node_id,
        .nodes = { std::move(node) },
        .panes = { std::move(pane) },
    };
}

TopologyTab TopologyService::make_initial_server_tab()
{
    TopologyPane pane{
        .pane_id = std::string(kServerShellPaneId),
        .name = "Server Shell",
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
        .root_node_id = node.node_id,
        .nodes = { std::move(node) },
        .panes = { std::move(pane) },
    };
}

void TopologyService::remember(
    std::string key, TopologyCommandResult result)
{
    if (completed_order_.size() >= kCompletedCommandLimit)
    {
        completed_.erase(completed_order_.front());
        completed_order_.pop_front();
    }
    completed_order_.push_back(key);
    completed_.emplace(std::move(key), std::move(result));
}

} // namespace draxul
