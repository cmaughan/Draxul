#include <draxul/topology_layout.h>

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <utility>

namespace draxul
{

namespace
{

std::unique_ptr<SessionSplitNode> convert_node(const TopologyTab& tab,
    std::string_view node_id, size_t depth,
    const TopologyTabLayoutOptions& options,
    std::unordered_set<std::string>& visited, TopologyTabLayout& out,
    std::string& error)
{
    const TopologyNode* source = find_node(tab, node_id);
    if (!source || depth > kTopologyMaxPanesPerTab
        || !visited.insert(source->node_id).second)
    {
        error = "Topology contains a missing or cyclic split node.";
        return {};
    }

    auto result = std::make_unique<SessionSplitNode>();
    result->is_leaf = source->is_leaf;
    result->direction
        = source->direction == TopologySplitDirection::Horizontal
        ? SplitDirection::Horizontal
        : SplitDirection::Vertical;
    result->ratio = source->ratio;
    if (source->is_leaf)
    {
        const TopologyPane* pane = find_pane(tab, source->pane_id);
        if (!pane)
        {
            error = "Topology split tree references a missing pane.";
            return {};
        }
        result->leaf_id = options.allocate_leaf(*source, *pane);
        out.leaf_by_pane[source->pane_id] = result->leaf_id;
        out.maximum_leaf = std::max(out.maximum_leaf, result->leaf_id);
        return result;
    }

    out.split_node_ids.push_back(source->node_id);
    result->first = convert_node(tab, source->first_node_id,
        depth + 1, options, visited, out, error);
    if (!result->first)
        return {};
    result->second = convert_node(tab, source->second_node_id,
        depth + 1, options, visited, out, error);
    if (!result->second)
        return {};
    return result;
}

} // namespace

const TopologySpace* find_space(
    const TopologySnapshot& snapshot, std::string_view space_id)
{
    const auto found = std::ranges::find(
        snapshot.spaces, space_id, &TopologySpace::space_id);
    return found == snapshot.spaces.end() ? nullptr : &*found;
}

TopologySpace* find_space(
    TopologySnapshot& snapshot, std::string_view space_id)
{
    return const_cast<TopologySpace*>(
        find_space(std::as_const(snapshot), space_id));
}

const TopologyTab* find_tab(
    const TopologySpace& space, std::string_view tab_id)
{
    const auto found = std::ranges::find(
        space.tabs, tab_id, &TopologyTab::tab_id);
    return found == space.tabs.end() ? nullptr : &*found;
}

TopologyTab* find_tab(TopologySpace& space, std::string_view tab_id)
{
    return const_cast<TopologyTab*>(
        find_tab(std::as_const(space), tab_id));
}

const TopologyPane* find_pane(
    const TopologyTab& tab, std::string_view pane_id)
{
    const auto found = std::ranges::find(
        tab.panes, pane_id, &TopologyPane::pane_id);
    return found == tab.panes.end() ? nullptr : &*found;
}

TopologyPane* find_pane(TopologyTab& tab, std::string_view pane_id)
{
    return const_cast<TopologyPane*>(
        find_pane(std::as_const(tab), pane_id));
}

const TopologyNode* find_node(
    const TopologyTab& tab, std::string_view node_id)
{
    const auto found = std::ranges::find(
        tab.nodes, node_id, &TopologyNode::node_id);
    return found == tab.nodes.end() ? nullptr : &*found;
}

TopologyNode* find_node(TopologyTab& tab, std::string_view node_id)
{
    return const_cast<TopologyNode*>(
        find_node(std::as_const(tab), node_id));
}

std::optional<TopologyTabLayout> topology_tab_to_layout(
    const TopologyTab& tab, const TopologyTabLayoutOptions& options,
    std::string& error)
{
    if (!options.allocate_leaf)
    {
        error = "Topology layout conversion requires a leaf allocator.";
        return std::nullopt;
    }

    TopologyTabLayout out;
    std::unordered_set<std::string> visited;
    out.layout.tree.root = convert_node(
        tab, tab.root_node_id, 0, options, visited, out, error);
    if (!out.layout.tree.root || visited.size() != tab.nodes.size())
    {
        if (error.empty())
            error = "Topology contains unreachable split nodes.";
        return std::nullopt;
    }
    out.layout.tree.next_leaf_id = out.maximum_leaf + 1;

    for (const TopologyPane& pane : tab.panes)
    {
        const auto leaf = out.leaf_by_pane.find(pane.pane_id);
        if (leaf == out.leaf_by_pane.end())
        {
            error = "Topology pane has no split-tree leaf.";
            return std::nullopt;
        }
        SessionPaneSnapshot saved{
            .leaf_id = leaf->second,
            .pane_name = pane.name,
            .pane_id = pane.pane_id,
            .agent = pane.agent,
            .agent_session = pane.agent_session,
            .restore_policy = pane.restore_policy,
        };
        if (pane.domain == TopologyPaneDomain::ServerTerminal)
        {
            saved.launch.kind = HostKind::RemoteTerminal;
            saved.launch.remote_terminal_id = pane.terminal_id;
            saved.launch.working_dir = pane.server_working_directory;
        }
        else
        {
            HostKind kind = options.fallback_host_kind;
            if (pane.client_host_kind
                != kTopologyPlatformDefaultHostKind)
            {
                if (const auto parsed
                    = parse_host_kind(pane.client_host_kind))
                {
                    kind = *parsed;
                }
            }
            saved.launch.kind = kind;
            saved.launch.client_host_kind = pane.client_host_kind;
            saved.launch.working_dir = pane.client_working_directory;
            saved.launch.source_path = pane.client_source_path;
            saved.launch.client_plugin_id = pane.client_plugin_id;
            saved.launch.client_plugin_config_json
                = pane.client_plugin_config_json;
            saved.launch.companion_owner_pane_id
                = pane.companion_owner_pane_id;
        }
        out.layout.panes.push_back(std::move(saved));
    }
    error.clear();
    return out;
}

std::function<LeafId(const TopologyNode&, const TopologyPane&)>
make_node_serial_leaf_allocator(int fallback_start)
{
    return [next = fallback_start](
               const TopologyNode& leaf, const TopologyPane&) mutable {
        return topology_id_serial(leaf.node_id, next++);
    };
}

} // namespace draxul
