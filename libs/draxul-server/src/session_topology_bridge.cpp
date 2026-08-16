#include "session_topology_bridge.h"

#include <draxul/session_state.h>
#include <draxul/topology_layout.h>

#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>

namespace draxul
{

namespace
{

std::optional<TabSnapshot> capture_tab(
    const TopologyTab& source, int fallback_id, std::string& error)
{
    TabSnapshot tab;
    tab.id = topology_id_serial(source.tab_id, fallback_id);
    tab.name = source.name;
    tab.name_user_set = source.name_user_set;

    const TopologyTabLayoutOptions options{
        // HostKind::Nvim is an inert placeholder for durable snapshots:
        // the preserved client_host_kind string (including the
        // platform-default sentinel) stays authoritative on restore.
        .fallback_host_kind = HostKind::Nvim,
        .allocate_leaf = make_node_serial_leaf_allocator(),
    };
    auto layout = topology_tab_to_layout(source, options, error);
    if (!layout)
        return std::nullopt;
    tab.pane_layout = std::move(layout->layout);
    tab.pane_layout.tree.focused_id
        = tab.pane_layout.tree.root->is_leaf
        ? tab.pane_layout.tree.root->leaf_id
        : layout->leaf_by_pane.at(source.panes.front().pane_id);
    return tab;
}

struct RestoreTabBuilder
{
    int next_internal_node = 1;
    std::unordered_set<std::string> used_node_ids;
    std::unordered_map<LeafId, std::string> panes;

    std::string unique_internal_node_id()
    {
        for (;;)
        {
            std::string candidate
                = "node-restored-" + std::to_string(next_internal_node++);
            if (used_node_ids.insert(candidate).second)
                return candidate;
        }
    }

    std::optional<std::string> add_node(
        const SessionSplitNode& source, TopologyTab& tab,
        std::string& error)
    {
        if (source.is_leaf)
        {
            const auto pane = panes.find(source.leaf_id);
            if (pane == panes.end())
            {
                error = "Session split tree references a missing pane.";
                return std::nullopt;
            }
            std::string node_id
                = "node-" + std::to_string(source.leaf_id);
            if (!used_node_ids.insert(node_id).second)
            {
                error = "Session split tree contains a duplicate leaf.";
                return std::nullopt;
            }
            tab.nodes.push_back(TopologyNode{
                .node_id = node_id,
                .is_leaf = true,
                .pane_id = pane->second,
            });
            return node_id;
        }

        const std::string node_id = unique_internal_node_id();
        if (!source.first || !source.second)
        {
            error = "Session split tree has an incomplete branch.";
            return std::nullopt;
        }
        auto first = add_node(*source.first, tab, error);
        if (!first)
            return std::nullopt;
        auto second = add_node(*source.second, tab, error);
        if (!second)
            return std::nullopt;
        tab.nodes.push_back(TopologyNode{
            .node_id = node_id,
            .is_leaf = false,
            .direction
            = source.direction == SplitDirection::Horizontal
                ? TopologySplitDirection::Horizontal
                : TopologySplitDirection::Vertical,
            .ratio = source.ratio,
            .first_node_id = std::move(*first),
            .second_node_id = std::move(*second),
        });
        return node_id;
    }
};

std::optional<TopologyTab> restore_tab(
    const TabSnapshot& source, SpaceId space_id,
    std::string& error)
{
    if (!source.pane_layout.tree.root
        || source.pane_layout.panes.empty())
    {
        error = "Session tab has no usable pane topology.";
        return std::nullopt;
    }

    TopologyTab tab{
        .tab_id = "tab-" + std::to_string(space_id)
            + "-" + std::to_string(source.id),
        .name = source.name,
        .name_user_set = source.name_user_set,
    };
    RestoreTabBuilder builder;
    for (const auto& pane : source.pane_layout.panes)
    {
        if (pane.pane_id.empty())
        {
            error = "Session tab contains an invalid pane identity.";
            return std::nullopt;
        }
        const std::string scoped_pane_id
            = "pane-" + std::to_string(space_id)
            + "-" + std::to_string(source.id)
            + "-" + std::to_string(pane.leaf_id);
        if (!builder.panes.emplace(
                              pane.leaf_id, scoped_pane_id)
                .second)
        {
            error = "Session tab contains an invalid pane identity.";
            return std::nullopt;
        }
        TopologyPane restored{
            .pane_id = scoped_pane_id,
            .name = pane.pane_name,
            .agent = pane.agent,
            .agent_session = pane.agent_session,
            .restore_policy = pane.restore_policy,
        };
        if (pane.launch.kind == HostKind::RemoteTerminal)
        {
            if (pane.launch.remote_terminal_id.empty())
            {
                error = "Remote terminal pane has no terminal identity.";
                return std::nullopt;
            }
            restored.domain = TopologyPaneDomain::ServerTerminal;
            restored.terminal_id = pane.launch.remote_terminal_id;
            restored.server_working_directory
                = pane.launch.working_dir;
        }
        else
        {
            restored.domain = TopologyPaneDomain::ClientLocal;
            restored.client_host_kind
                = pane.launch.client_host_kind.empty()
                ? to_string(pane.launch.kind)
                : pane.launch.client_host_kind;
            restored.client_working_directory
                = pane.launch.working_dir;
            restored.client_source_path
                = pane.launch.source_path;
            restored.client_plugin_id
                = pane.launch.client_plugin_id;
            restored.client_plugin_config_json
                = pane.launch.client_plugin_config_json;
            restored.companion_owner_pane_id
                = pane.launch.companion_owner_pane_id;
        }
        tab.panes.push_back(std::move(restored));
    }
    auto root = builder.add_node(
        *source.pane_layout.tree.root, tab, error);
    if (!root)
        return std::nullopt;
    tab.root_node_id = std::move(*root);
    return tab;
}

} // namespace

std::optional<SessionSnapshot> capture_session_topology(
    const TopologySnapshot& topology, std::string& error)
{
    SessionSnapshot session{
        .session_id = topology.session_id.empty()
            ? "default"
            : topology.session_id,
        .session_name = topology.session_id.empty()
            ? "default"
            : topology.session_id,
    };
    int fallback_space = 1'000'000;
    int fallback_tab = 1'000'000;
    for (const auto& source_space : topology.spaces)
    {
        SpaceSnapshot space{
            .id = topology_id_serial(
                source_space.space_id, fallback_space++),
            .name = source_space.name,
            .root_directory = source_space.root_directory,
        };
        for (const auto& source_tab : source_space.tabs)
        {
            auto tab = capture_tab(
                source_tab, fallback_tab++, error);
            if (!tab)
                return std::nullopt;
            space.next_tab_id = std::max(
                space.next_tab_id, tab->id + 1);
            space.tabs.push_back(std::move(*tab));
        }
        if (!space.tabs.empty())
            space.active_tab_id = space.tabs.front().id;
        session.next_space_id
            = std::max(session.next_space_id, space.id + 1);
        session.spaces.push_back(std::move(space));
    }
    if (!session.spaces.empty())
        session.active_space_id = session.spaces.front().id;
    if (!validate_session_snapshot(session, &error))
        return std::nullopt;
    return session;
}

std::optional<SessionTopologyRestore> restore_session_topology(
    const SessionSnapshot& session, std::string& error)
{
    if (!validate_session_snapshot(session, &error))
        return std::nullopt;

    SessionTopologyRestore restored;
    restored.topology.revision = 1;
    restored.topology.session_id = session.session_id.empty()
        ? "default"
        : session.session_id;
    for (const auto& source_space : session.spaces)
    {
        TopologySpace space{
            .space_id = "space-" + std::to_string(source_space.id),
            .name = source_space.name,
            .root_directory = source_space.root_directory.string(),
        };
        for (const auto& source_tab : source_space.tabs)
        {
            std::string tab_error;
            auto tab = restore_tab(
                source_tab, source_space.id, tab_error);
            if (!tab)
            {
                restored.warnings.push_back(
                    "Skipped tab '" + source_tab.name + "': "
                    + tab_error);
                continue;
            }
            space.tabs.push_back(std::move(*tab));
        }
        if (space.tabs.empty())
        {
            restored.warnings.push_back(
                "Skipped Space '" + source_space.name
                + "' because it has no usable tabs.");
            continue;
        }
        restored.topology.spaces.push_back(std::move(space));
    }
    if (restored.topology.spaces.empty())
    {
        error = "Saved Session contains no usable Spaces.";
        return std::nullopt;
    }

    std::string protocol_error;
    const auto validated = topology_snapshot_from_json(
        topology_snapshot_to_json(restored.topology),
        protocol_error);
    if (!validated)
    {
        error = "Restored topology is invalid: " + protocol_error;
        return std::nullopt;
    }
    restored.topology = std::move(*validated);
    return restored;
}

} // namespace draxul
