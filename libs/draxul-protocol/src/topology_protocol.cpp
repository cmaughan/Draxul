#include <draxul/topology_protocol.h>

#include <cmath>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace draxul
{

namespace
{

bool valid_text(const std::string& value, bool allow_empty = false)
{
    return (allow_empty || !value.empty())
        && value.size() <= kTopologyMaxTextBytes;
}

bool read_string(const nlohmann::json& value, std::string_view key,
    std::string& target, bool allow_empty = false)
{
    if (!value.contains(key) || !value[key].is_string())
        return false;
    target = value[key].get<std::string>();
    return valid_text(target, allow_empty);
}

nlohmann::json pane_to_json(const TopologyPane& pane)
{
    return {
        { "pane_id", pane.pane_id },
        { "name", pane.name },
        { "domain", to_string(pane.domain) },
        { "terminal_id", pane.terminal_id },
        { "client_host_kind", pane.client_host_kind },
    };
}

bool read_pane(const nlohmann::json& value, TopologyPane& pane)
{
    std::string domain;
    if (!value.is_object()
        || !read_string(value, "pane_id", pane.pane_id)
        || !read_string(value, "name", pane.name, true)
        || !read_string(value, "domain", domain)
        || !read_string(value, "terminal_id", pane.terminal_id, true)
        || !read_string(value, "client_host_kind",
            pane.client_host_kind, true))
    {
        return false;
    }
    const auto parsed_domain = parse_topology_pane_domain(domain);
    if (!parsed_domain)
        return false;
    pane.domain = *parsed_domain;
    return pane.domain == TopologyPaneDomain::ServerTerminal
        ? !pane.terminal_id.empty() && pane.client_host_kind.empty()
        : pane.terminal_id.empty() && !pane.client_host_kind.empty();
}

nlohmann::json node_to_json(const TopologyNode& node)
{
    return {
        { "node_id", node.node_id },
        { "is_leaf", node.is_leaf },
        { "pane_id", node.pane_id },
        { "direction", to_string(node.direction) },
        { "ratio", node.ratio },
        { "first_node_id", node.first_node_id },
        { "second_node_id", node.second_node_id },
    };
}

bool read_node(const nlohmann::json& value, TopologyNode& node)
{
    std::string direction;
    if (!value.is_object()
        || !read_string(value, "node_id", node.node_id)
        || !value.contains("is_leaf") || !value["is_leaf"].is_boolean()
        || !read_string(value, "pane_id", node.pane_id, true)
        || !read_string(value, "direction", direction)
        || !value.contains("ratio") || !value["ratio"].is_number()
        || !read_string(value, "first_node_id",
            node.first_node_id, true)
        || !read_string(value, "second_node_id",
            node.second_node_id, true))
    {
        return false;
    }
    node.is_leaf = value["is_leaf"].get<bool>();
    node.ratio = value["ratio"].get<float>();
    const auto parsed_direction
        = parse_topology_split_direction(direction);
    if (!parsed_direction || !std::isfinite(node.ratio)
        || node.ratio < 0.1f || node.ratio > 0.9f)
    {
        return false;
    }
    node.direction = *parsed_direction;
    return node.is_leaf
        ? !node.pane_id.empty() && node.first_node_id.empty()
            && node.second_node_id.empty()
        : node.pane_id.empty() && !node.first_node_id.empty()
            && !node.second_node_id.empty();
}

nlohmann::json tab_to_json(const TopologyTab& tab)
{
    nlohmann::json nodes = nlohmann::json::array();
    for (const auto& node : tab.nodes)
        nodes.push_back(node_to_json(node));
    nlohmann::json panes = nlohmann::json::array();
    for (const auto& pane : tab.panes)
        panes.push_back(pane_to_json(pane));
    return {
        { "tab_id", tab.tab_id },
        { "name", tab.name },
        { "root_node_id", tab.root_node_id },
        { "nodes", std::move(nodes) },
        { "panes", std::move(panes) },
    };
}

bool read_tab(const nlohmann::json& value, TopologyTab& tab)
{
    if (!value.is_object()
        || !read_string(value, "tab_id", tab.tab_id)
        || !read_string(value, "name", tab.name)
        || !read_string(value, "root_node_id", tab.root_node_id)
        || !value.contains("nodes") || !value["nodes"].is_array()
        || !value.contains("panes") || !value["panes"].is_array()
        || value["panes"].empty()
        || value["panes"].size() > kTopologyMaxPanesPerTab
        || value["nodes"].empty()
        || value["nodes"].size() > kTopologyMaxPanesPerTab * 2 - 1)
    {
        return false;
    }

    std::unordered_set<std::string> pane_ids;
    for (const auto& item : value["panes"])
    {
        TopologyPane pane;
        if (!read_pane(item, pane)
            || !pane_ids.insert(pane.pane_id).second)
        {
            return false;
        }
        tab.panes.push_back(std::move(pane));
    }

    std::unordered_set<std::string> node_ids;
    for (const auto& item : value["nodes"])
    {
        TopologyNode node;
        if (!read_node(item, node)
            || !node_ids.insert(node.node_id).second)
        {
            return false;
        }
        tab.nodes.push_back(std::move(node));
    }
    if (!node_ids.contains(tab.root_node_id))
        return false;
    std::unordered_set<std::string> referenced_panes;
    for (const auto& node : tab.nodes)
    {
        if (node.is_leaf)
        {
            if (!pane_ids.contains(node.pane_id)
                || !referenced_panes.insert(node.pane_id).second)
                return false;
        }
        else if (!node_ids.contains(node.first_node_id)
            || !node_ids.contains(node.second_node_id)
            || node.first_node_id == node.second_node_id)
        {
            return false;
        }
    }
    return referenced_panes.size() == pane_ids.size()
        && tab.nodes.size() == tab.panes.size() * 2 - 1;
}

nlohmann::json space_to_json(const TopologySpace& space)
{
    nlohmann::json tabs = nlohmann::json::array();
    for (const auto& tab : space.tabs)
        tabs.push_back(tab_to_json(tab));
    return {
        { "space_id", space.space_id },
        { "name", space.name },
        { "root_directory", space.root_directory },
        { "tabs", std::move(tabs) },
    };
}

bool read_space(const nlohmann::json& value, TopologySpace& space)
{
    if (!value.is_object()
        || !read_string(value, "space_id", space.space_id)
        || !read_string(value, "name", space.name)
        || !read_string(value, "root_directory",
            space.root_directory, true)
        || !value.contains("tabs") || !value["tabs"].is_array()
        || value["tabs"].empty()
        || value["tabs"].size() > kTopologyMaxTabsPerSpace)
    {
        return false;
    }
    std::unordered_set<std::string> tab_ids;
    for (const auto& item : value["tabs"])
    {
        TopologyTab tab;
        if (!read_tab(item, tab)
            || !tab_ids.insert(tab.tab_id).second)
        {
            return false;
        }
        space.tabs.push_back(std::move(tab));
    }
    return true;
}

} // namespace

std::string_view to_string(TopologyPaneDomain domain)
{
    switch (domain)
    {
    case TopologyPaneDomain::ServerTerminal:
        return "server_terminal";
    case TopologyPaneDomain::ClientLocal:
        return "client_local";
    }
    return "client_local";
}

std::optional<TopologyPaneDomain> parse_topology_pane_domain(
    std::string_view value)
{
    if (value == "server_terminal")
        return TopologyPaneDomain::ServerTerminal;
    if (value == "client_local")
        return TopologyPaneDomain::ClientLocal;
    return std::nullopt;
}

std::string_view to_string(TopologySplitDirection direction)
{
    switch (direction)
    {
    case TopologySplitDirection::Vertical:
        return "vertical";
    case TopologySplitDirection::Horizontal:
        return "horizontal";
    }
    return "vertical";
}

std::optional<TopologySplitDirection> parse_topology_split_direction(
    std::string_view value)
{
    if (value == "vertical")
        return TopologySplitDirection::Vertical;
    if (value == "horizontal")
        return TopologySplitDirection::Horizontal;
    return std::nullopt;
}

std::string_view to_string(TopologyCommandKind kind)
{
    switch (kind)
    {
    case TopologyCommandKind::CreateSpace:
        return "create_space";
    case TopologyCommandKind::RenameSpace:
        return "rename_space";
    case TopologyCommandKind::CloseSpace:
        return "close_space";
    case TopologyCommandKind::CreateTab:
        return "create_tab";
    case TopologyCommandKind::RenameTab:
        return "rename_tab";
    case TopologyCommandKind::CloseTab:
        return "close_tab";
    case TopologyCommandKind::MoveTab:
        return "move_tab";
    case TopologyCommandKind::SplitPane:
        return "split_pane";
    case TopologyCommandKind::ClosePane:
        return "close_pane";
    case TopologyCommandKind::RenamePane:
        return "rename_pane";
    case TopologyCommandKind::SwapPane:
        return "swap_pane";
    case TopologyCommandKind::RestartPane:
        return "restart_pane";
    case TopologyCommandKind::SetSplitRatio:
        return "set_split_ratio";
    case TopologyCommandKind::EqualizeSplits:
        return "equalize_splits";
    }
    return "create_space";
}

std::optional<TopologyCommandKind> parse_topology_command_kind(
    std::string_view value)
{
    if (value == "create_space")
        return TopologyCommandKind::CreateSpace;
    if (value == "rename_space")
        return TopologyCommandKind::RenameSpace;
    if (value == "close_space")
        return TopologyCommandKind::CloseSpace;
    if (value == "create_tab")
        return TopologyCommandKind::CreateTab;
    if (value == "rename_tab")
        return TopologyCommandKind::RenameTab;
    if (value == "close_tab")
        return TopologyCommandKind::CloseTab;
    if (value == "move_tab")
        return TopologyCommandKind::MoveTab;
    if (value == "split_pane")
        return TopologyCommandKind::SplitPane;
    if (value == "close_pane")
        return TopologyCommandKind::ClosePane;
    if (value == "rename_pane")
        return TopologyCommandKind::RenamePane;
    if (value == "swap_pane")
        return TopologyCommandKind::SwapPane;
    if (value == "restart_pane")
        return TopologyCommandKind::RestartPane;
    if (value == "set_split_ratio")
        return TopologyCommandKind::SetSplitRatio;
    if (value == "equalize_splits")
        return TopologyCommandKind::EqualizeSplits;
    return std::nullopt;
}

nlohmann::json topology_snapshot_to_json(
    const TopologySnapshot& snapshot)
{
    nlohmann::json spaces = nlohmann::json::array();
    for (const auto& space : snapshot.spaces)
        spaces.push_back(space_to_json(space));
    return {
        { "revision", snapshot.revision },
        { "session_id", snapshot.session_id },
        { "spaces", std::move(spaces) },
    };
}

std::optional<TopologySnapshot> topology_snapshot_from_json(
    const nlohmann::json& value, std::string& error)
{
    TopologySnapshot snapshot;
    if (!value.is_object()
        || !value.contains("revision")
        || !value["revision"].is_number_unsigned()
        || !read_string(value, "session_id", snapshot.session_id)
        || !value.contains("spaces") || !value["spaces"].is_array()
        || value["spaces"].empty()
        || value["spaces"].size() > kTopologyMaxSpaces)
    {
        error = "Invalid topology snapshot.";
        return std::nullopt;
    }
    snapshot.revision = value["revision"].get<uint64_t>();
    std::unordered_set<std::string> space_ids;
    for (const auto& item : value["spaces"])
    {
        TopologySpace space;
        if (!read_space(item, space)
            || !space_ids.insert(space.space_id).second)
        {
            error = "Invalid topology Space.";
            return std::nullopt;
        }
        snapshot.spaces.push_back(std::move(space));
    }
    return snapshot;
}

nlohmann::json topology_command_to_json(
    const TopologyCommand& command)
{
    return {
        { "client_id", command.client_id },
        { "command_id", command.command_id },
        { "expected_revision", command.expected_revision },
        { "kind", to_string(command.kind) },
        { "space_id", command.space_id },
        { "tab_id", command.tab_id },
        { "pane_id", command.pane_id },
        { "target_pane_id", command.target_pane_id },
        { "node_id", command.node_id },
        { "name", command.name },
        { "root_directory", command.root_directory },
        { "direction", to_string(command.direction) },
        { "ratio", command.ratio },
        { "move_delta", command.move_delta },
        { "pane_domain", to_string(command.pane_domain) },
        { "terminal_id", command.terminal_id },
        { "client_host_kind", command.client_host_kind },
    };
}

std::optional<TopologyCommand> topology_command_from_json(
    const nlohmann::json& value, std::string& error)
{
    TopologyCommand command;
    std::string kind;
    std::string direction;
    std::string pane_domain;
    if (!value.is_object()
        || !read_string(value, "client_id", command.client_id)
        || !read_string(value, "command_id", command.command_id)
        || !value.contains("expected_revision")
        || !value["expected_revision"].is_number_unsigned()
        || !read_string(value, "kind", kind)
        || !read_string(value, "space_id", command.space_id, true)
        || !read_string(value, "tab_id", command.tab_id, true)
        || !read_string(value, "pane_id", command.pane_id, true)
        || !read_string(value, "node_id", command.node_id, true)
        || !read_string(value, "name", command.name, true)
        || !read_string(value, "root_directory",
            command.root_directory, true)
        || !read_string(value, "direction", direction)
        || !value.contains("ratio") || !value["ratio"].is_number()
        || !read_string(value, "pane_domain", pane_domain)
        || !read_string(value, "terminal_id",
            command.terminal_id, true)
        || !read_string(value, "client_host_kind",
            command.client_host_kind, true))
    {
        error = "Invalid topology command.";
        return std::nullopt;
    }
    if (value.contains("target_pane_id")
        && !read_string(value, "target_pane_id",
            command.target_pane_id, true))
    {
        error = "Invalid topology command target pane.";
        return std::nullopt;
    }
    if (value.contains("move_delta"))
    {
        if (!value["move_delta"].is_number_integer())
        {
            error = "Invalid topology command move delta.";
            return std::nullopt;
        }
        command.move_delta = value["move_delta"].get<int>();
    }
    const auto parsed_kind = parse_topology_command_kind(kind);
    const auto parsed_direction
        = parse_topology_split_direction(direction);
    const auto parsed_domain
        = parse_topology_pane_domain(pane_domain);
    command.ratio = value["ratio"].get<float>();
    if (!parsed_kind || !parsed_direction || !parsed_domain
        || !std::isfinite(command.ratio))
    {
        error = "Invalid topology command enum or ratio.";
        return std::nullopt;
    }
    command.expected_revision
        = value["expected_revision"].get<uint64_t>();
    command.kind = *parsed_kind;
    command.direction = *parsed_direction;
    command.pane_domain = *parsed_domain;
    return command;
}

nlohmann::json topology_command_result_to_json(
    const TopologyCommandResult& result)
{
    return {
        { "applied", result.applied },
        { "duplicate", result.duplicate },
        { "snapshot", topology_snapshot_to_json(result.snapshot) },
    };
}

std::optional<TopologyCommandResult>
topology_command_result_from_json(
    const nlohmann::json& value, std::string& error)
{
    if (!value.is_object()
        || !value.contains("applied") || !value["applied"].is_boolean()
        || !value.contains("duplicate")
        || !value["duplicate"].is_boolean()
        || !value.contains("snapshot"))
    {
        error = "Invalid topology command result.";
        return std::nullopt;
    }
    auto snapshot = topology_snapshot_from_json(value["snapshot"], error);
    if (!snapshot)
        return std::nullopt;
    return TopologyCommandResult{
        .applied = value["applied"].get<bool>(),
        .duplicate = value["duplicate"].get<bool>(),
        .snapshot = std::move(*snapshot),
    };
}

} // namespace draxul
