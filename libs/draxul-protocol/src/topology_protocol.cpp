#include <draxul/topology_protocol.h>

#include "json_extract.h"

#include <draxul/server_protocol.h>

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

nlohmann::json agent_to_json(const TopologyPane& pane)
{
    nlohmann::json agent{
        { "profile_id", pane.agent->profile_id },
        { "kind", pane.agent->kind },
        { "display_name", pane.agent->display_name },
        { "instance_id", pane.agent->instance_id },
        { "origin", to_string(pane.agent->origin) },
        { "restore_policy", to_string(pane.restore_policy) },
    };
    if (pane.agent_session)
    {
        agent["session"] = {
            { "source", pane.agent_session->source },
            { "agent_kind", pane.agent_session->agent_kind },
            { "integration_version",
                pane.agent_session->integration_version },
            { "sequence", pane.agent_session->sequence },
            { "kind", to_string(pane.agent_session->kind) },
            { "value", pane.agent_session->value },
        };
    }
    return agent;
}

bool read_agent(const nlohmann::json& value, TopologyPane& pane)
{
    if (!value.is_object())
        return false;
    AgentIdentity identity;
    std::string origin;
    std::string policy;
    if (!read_string(value, "profile_id", identity.profile_id, true)
        || !read_string(value, "kind", identity.kind)
        || !read_string(value, "display_name", identity.display_name)
        || !read_string(value, "instance_id", identity.instance_id)
        || !read_string(value, "origin", origin)
        || !read_string(value, "restore_policy", policy))
    {
        return false;
    }
    if (origin == "managed")
        identity.origin = AgentIdentityOrigin::Managed;
    else if (origin == "discovered")
        identity.origin = AgentIdentityOrigin::Discovered;
    else
        return false;
    const auto restore_policy = parse_agent_restore_policy(policy);
    if (!restore_policy)
        return false;
    pane.agent = std::move(identity);
    pane.restore_policy = *restore_policy;

    if (!value.contains("session"))
        return true;
    const auto& session_value = value["session"];
    AgentSessionRef session;
    std::string kind;
    if (!session_value.is_object()
        || !read_string(session_value, "source", session.source)
        || !read_string(
            session_value, "agent_kind", session.agent_kind)
        || !session_value.contains("integration_version")
        || !session_value["integration_version"].is_number_unsigned()
        || !session_value.contains("sequence")
        || !session_value["sequence"].is_number_unsigned()
        || !read_string(session_value, "kind", kind)
        || !read_string(session_value, "value", session.value))
    {
        return false;
    }
    if (!read_bounded_integer(
            session_value["integration_version"],
            session.integration_version)
        || !read_bounded_integer(
            session_value["sequence"], session.sequence))
    {
        return false;
    }
    const auto ref_kind = parse_agent_session_ref_kind(kind);
    if (!ref_kind)
        return false;
    session.kind = *ref_kind;
    if (!validate_agent_session_ref(session))
        return false;
    pane.agent_session = std::move(session);
    return true;
}

nlohmann::json pane_to_json(const TopologyPane& pane)
{
    nlohmann::json result{
        { "pane_id", pane.pane_id },
        { "name", pane.name },
        { "domain", to_string(pane.domain) },
        { "terminal_id", pane.terminal_id },
        { "client_host_kind", pane.client_host_kind },
        { "client_working_directory",
            pane.client_working_directory },
        { "client_source_path", pane.client_source_path },
        { "client_plugin_id", pane.client_plugin_id },
        { "client_plugin_config_json", pane.client_plugin_config_json },
        { "companion_owner_pane_id",
            pane.companion_owner_pane_id },
        { "server_working_directory",
            pane.server_working_directory },
    };
    if (pane.agent)
        result["agent"] = agent_to_json(pane);
    return result;
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
    if (value.contains("server_working_directory")
        && !read_string(value, "server_working_directory",
            pane.server_working_directory, true))
    {
        return false;
    }
    if ((value.contains("client_working_directory")
            && !read_string(value, "client_working_directory",
                pane.client_working_directory, true))
        || (value.contains("client_source_path")
            && !read_string(value, "client_source_path",
                pane.client_source_path, true))
        || (value.contains("client_plugin_id")
            && !read_string(value, "client_plugin_id",
                pane.client_plugin_id, true))
        || (value.contains("client_plugin_config_json")
            && !read_string(value, "client_plugin_config_json",
                pane.client_plugin_config_json, true))
        || (value.contains("companion_owner_pane_id")
            && !read_string(value, "companion_owner_pane_id",
                pane.companion_owner_pane_id, true)))
    {
        return false;
    }
    const auto parsed_domain = parse_topology_pane_domain(domain);
    if (!parsed_domain)
        return false;
    pane.domain = *parsed_domain;
    const bool valid_domain
        = pane.domain == TopologyPaneDomain::ServerTerminal
        ? !pane.terminal_id.empty() && pane.client_host_kind.empty()
        : pane.terminal_id.empty() && !pane.client_host_kind.empty();
    if (!valid_domain)
        return false;
    if (value.contains("agent")
        && !read_agent(value["agent"], pane))
    {
        return false;
    }
    return !pane.agent_session || pane.agent.has_value();
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
        || node.ratio < kTopologyMinSplitRatio
        || node.ratio > kTopologyMaxSplitRatio)
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
        { "name_user_set", tab.name_user_set },
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
        || (value.contains("name_user_set")
            && !value["name_user_set"].is_boolean())
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
    tab.name_user_set
        = value.value("name_user_set", true);

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
    case TopologyCommandKind::MovePane:
        return "move_pane";
    case TopologyCommandKind::UpdateClientPane:
        return "update_client_pane";
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
    if (value == "move_pane")
        return TopologyCommandKind::MovePane;
    if (value == "update_client_pane")
        return TopologyCommandKind::UpdateClientPane;
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
    if (!read_bounded_integer(
            value["revision"], snapshot.revision))
    {
        error = "Invalid topology snapshot revision.";
        return std::nullopt;
    }
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
        { "place_before", command.place_before },
        { "move_delta", command.move_delta },
        { "pane_domain", to_string(command.pane_domain) },
        { "terminal_id", command.terminal_id },
        { "client_host_kind", command.client_host_kind },
        { "client_working_directory",
            command.client_working_directory },
        { "client_source_path", command.client_source_path },
        { "client_plugin_id", command.client_plugin_id },
        { "client_plugin_config_json", command.client_plugin_config_json },
        { "companion_owner_pane_id",
            command.companion_owner_pane_id },
        { "server_working_directory",
            command.server_working_directory },
    };
}

std::optional<TopologyCommand> topology_command_from_json(
    const nlohmann::json& value, std::string& error)
{
    TopologyCommand command;
    std::string kind;
    if (!value.is_object()
        || !read_string(value, "client_id", command.client_id)
        || !read_string(value, "command_id", command.command_id)
        || !value.contains("expected_revision")
        || !read_string(value, "kind", kind))
    {
        error = "Invalid topology command.";
        return std::nullopt;
    }

    const auto read_optional_string
        = [&value](std::string_view key, std::string& target) {
              return !value.contains(key)
                  || read_string(value, key, target, true);
          };
    if (!read_optional_string("space_id", command.space_id)
        || !read_optional_string("tab_id", command.tab_id)
        || !read_optional_string("pane_id", command.pane_id)
        || !read_optional_string(
            "target_pane_id", command.target_pane_id)
        || !read_optional_string("node_id", command.node_id)
        || !read_optional_string("name", command.name)
        || !read_optional_string(
            "root_directory", command.root_directory)
        || !read_optional_string(
            "terminal_id", command.terminal_id)
        || !read_optional_string(
            "client_host_kind", command.client_host_kind)
        || !read_optional_string(
            "client_working_directory",
            command.client_working_directory)
        || !read_optional_string(
            "client_source_path", command.client_source_path)
        || !read_optional_string(
            "client_plugin_id", command.client_plugin_id)
        || !read_optional_string(
            "client_plugin_config_json", command.client_plugin_config_json)
        || !read_optional_string(
            "companion_owner_pane_id",
            command.companion_owner_pane_id)
        || !read_optional_string(
            "server_working_directory",
            command.server_working_directory))
    {
        error = "Invalid optional topology command text.";
        return std::nullopt;
    }
    if (!valid_server_client_id(command.client_id))
    {
        error = "Invalid topology command client identity.";
        return std::nullopt;
    }
    if (value.contains("move_delta"))
    {
        if (!read_bounded_integer(
                value["move_delta"], command.move_delta))
        {
            error = "Invalid topology command move delta.";
            return std::nullopt;
        }
    }
    if (value.contains("place_before"))
    {
        if (!value["place_before"].is_boolean())
        {
            error = "Invalid topology command placement.";
            return std::nullopt;
        }
        command.place_before = value["place_before"].get<bool>();
    }
    const auto parsed_kind = parse_topology_command_kind(kind);
    if (!parsed_kind)
    {
        error = "Invalid topology command kind.";
        return std::nullopt;
    }
    if (value.contains("direction"))
    {
        std::string direction;
        if (!read_string(value, "direction", direction))
        {
            error = "Invalid topology command direction.";
            return std::nullopt;
        }
        const auto parsed_direction
            = parse_topology_split_direction(direction);
        if (!parsed_direction)
        {
            error = "Invalid topology command direction.";
            return std::nullopt;
        }
        command.direction = *parsed_direction;
    }
    if (value.contains("ratio"))
    {
        if (!value["ratio"].is_number())
        {
            error = "Invalid topology command ratio.";
            return std::nullopt;
        }
        command.ratio = value["ratio"].get<float>();
        if (!std::isfinite(command.ratio))
        {
            error = "Invalid topology command ratio.";
            return std::nullopt;
        }
    }
    if (value.contains("pane_domain"))
    {
        std::string pane_domain;
        if (!read_string(value, "pane_domain", pane_domain))
        {
            error = "Invalid topology command pane domain.";
            return std::nullopt;
        }
        const auto parsed_domain
            = parse_topology_pane_domain(pane_domain);
        if (!parsed_domain)
        {
            error = "Invalid topology command pane domain.";
            return std::nullopt;
        }
        command.pane_domain = *parsed_domain;
    }
    if (!read_bounded_integer(
            value["expected_revision"],
            command.expected_revision))
    {
        error = "Invalid topology command revision.";
        return std::nullopt;
    }
    command.kind = *parsed_kind;
    return command;
}

nlohmann::json topology_command_result_to_json(
    const TopologyCommandResult& result)
{
    return {
        { "applied", result.applied },
        { "duplicate", result.duplicate },
        { "created_id", result.created_id },
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
    std::string created_id;
    if (value.contains("created_id")
        && !read_string(
            value, "created_id", created_id, true))
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
        .created_id = std::move(created_id),
        .snapshot = std::move(*snapshot),
    };
}

} // namespace draxul
