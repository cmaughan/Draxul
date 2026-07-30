#include <draxul/topology_projection.h>

#include <draxul/remote_session_client.h>

#include <algorithm>
#include <functional>
#include <sstream>

namespace draxul
{

bool TopologyProjection::empty() const noexcept
{
    return space_to_local_.empty();
}

void TopologyProjection::bind_space(
    std::string remote_id, SpaceId local_id)
{
    if (const auto previous = local_to_space_.find(local_id);
        previous != local_to_space_.end())
    {
        space_to_local_.erase(previous->second);
    }
    if (const auto previous = space_to_local_.find(remote_id);
        previous != space_to_local_.end())
    {
        local_to_space_.erase(previous->second);
    }
    local_to_space_[local_id] = remote_id;
    space_to_local_[std::move(remote_id)] = local_id;
}

std::optional<SpaceId> TopologyProjection::local_space(
    std::string_view remote_id) const
{
    const auto found = space_to_local_.find(std::string(remote_id));
    return found == space_to_local_.end()
        ? std::nullopt
        : std::optional<SpaceId>(found->second);
}

std::optional<std::string> TopologyProjection::remote_space(
    SpaceId local_id) const
{
    const auto found = local_to_space_.find(local_id);
    return found == local_to_space_.end()
        ? std::nullopt
        : std::optional<std::string>(found->second);
}

void TopologyProjection::erase_space(
    std::string_view remote_id)
{
    const auto found = space_to_local_.find(std::string(remote_id));
    if (found == space_to_local_.end())
        return;
    local_to_space_.erase(found->second);
    space_to_local_.erase(found);
}

const std::unordered_map<std::string, SpaceId>&
TopologyProjection::spaces() const noexcept
{
    return space_to_local_;
}

void TopologyProjection::bind_tab(std::string remote_id,
    SpaceId local_space_id, int local_tab_id)
{
    tab_to_local_[std::move(remote_id)]
        = { local_space_id, local_tab_id };
}

std::optional<std::pair<SpaceId, int>>
TopologyProjection::local_tab(std::string_view remote_id) const
{
    const auto found = tab_to_local_.find(std::string(remote_id));
    return found == tab_to_local_.end()
        ? std::nullopt
        : std::optional(found->second);
}

void TopologyProjection::erase_tab(
    std::string_view remote_id)
{
    const std::string key(remote_id);
    tab_to_local_.erase(key);
    tab_layout_signatures_.erase(key);
    tab_divider_nodes_.erase(key);
}

const std::unordered_map<std::string,
    std::pair<SpaceId, int>>&
TopologyProjection::tabs() const noexcept
{
    return tab_to_local_;
}

std::optional<LeafId> TopologyProjection::local_pane(
    std::string_view remote_id) const
{
    const auto found = pane_to_leaf_.find(std::string(remote_id));
    return found == pane_to_leaf_.end()
        ? std::nullopt
        : std::optional<LeafId>(found->second);
}

void TopologyProjection::prune_panes(
    const std::unordered_set<std::string>& live_remote_ids)
{
    std::erase_if(pane_to_leaf_, [&](const auto& item) {
        return !live_remote_ids.contains(item.first);
    });
}

std::optional<TopologyTabProjection>
TopologyProjection::project_tab(const TopologyTab& remote,
    LeafId focused_leaf, HostKind platform_default_kind,
    std::string& error)
{
    TopologyTabProjection projection;
    std::ostringstream signature;
    signature << remote.root_node_id << '|';
    for (const TopologyNode& node : remote.nodes)
    {
        signature << node.node_id << ':' << node.is_leaf << ':'
                  << node.pane_id << ':'
                  << static_cast<int>(node.direction) << ':'
                  << node.ratio << ':' << node.first_node_id << ':'
                  << node.second_node_id << ';';
    }
    signature << '|';
    for (const TopologyPane& pane : remote.panes)
    {
        signature << pane.pane_id << ':'
                  << static_cast<int>(pane.domain) << ':'
                  << pane.terminal_id << ':'
                  << pane.client_host_kind << ';';
    }
    projection.structural_signature = signature.str();
    if (const auto previous
        = tab_layout_signatures_.find(remote.tab_id);
        previous != tab_layout_signatures_.end()
        && previous->second == projection.structural_signature)
    {
        projection.requires_reconcile = false;
        for (const TopologyPane& pane : remote.panes)
        {
            if (const auto leaf = local_pane(pane.pane_id))
                projection.pane_names.emplace_back(*leaf, pane.name);
        }
        error.clear();
        return projection;
    }

    std::unordered_map<std::string, const TopologyNode*> nodes;
    std::unordered_map<std::string, const TopologyPane*> panes;
    for (const TopologyNode& node : remote.nodes)
        nodes.emplace(node.node_id, &node);
    for (const TopologyPane& pane : remote.panes)
        panes.emplace(pane.pane_id, &pane);

    std::unordered_set<std::string> visiting;
    std::unordered_set<std::string> projected_panes;
    DividerId next_divider_id = 0;
    LeafId maximum_leaf = kInvalidLeaf;
    std::function<std::unique_ptr<SessionSplitNode>(
        const std::string&, size_t)>
        project_node;
    project_node = [&](const std::string& node_id, size_t depth)
        -> std::unique_ptr<SessionSplitNode> {
        if (depth > kTopologyMaxPanesPerTab
            || visiting.contains(node_id))
        {
            return nullptr;
        }
        const auto found = nodes.find(node_id);
        if (found == nodes.end())
            return nullptr;
        visiting.insert(node_id);
        const TopologyNode& source = *found->second;
        auto result = std::make_unique<SessionSplitNode>();
        result->is_leaf = source.is_leaf;
        if (source.is_leaf)
        {
            const auto pane = panes.find(source.pane_id);
            if (pane == panes.end())
                return nullptr;
            auto leaf = pane_to_leaf_.find(source.pane_id);
            if (leaf == pane_to_leaf_.end())
            {
                leaf = pane_to_leaf_
                           .emplace(source.pane_id, next_leaf_id_++)
                           .first;
            }
            result->leaf_id = leaf->second;
            maximum_leaf = std::max(maximum_leaf, leaf->second);
            projected_panes.insert(source.pane_id);
        }
        else
        {
            projection.divider_nodes.emplace(
                next_divider_id++, source.node_id);
            result->direction
                = source.direction
                    == TopologySplitDirection::Vertical
                ? SplitDirection::Vertical
                : SplitDirection::Horizontal;
            result->ratio = source.ratio;
            result->first
                = project_node(source.first_node_id, depth + 1);
            result->second
                = project_node(source.second_node_id, depth + 1);
            if (!result->first || !result->second)
                return nullptr;
        }
        visiting.erase(node_id);
        return result;
    };

    projection.layout.tree.root
        = project_node(remote.root_node_id, 0);
    if (!projection.layout.tree.root
        || projected_panes.size() != remote.panes.size())
    {
        error = "Server tab contains an invalid split tree.";
        return std::nullopt;
    }

    bool focused_survives = false;
    for (const std::string& pane_id : projected_panes)
    {
        if (local_pane(pane_id) == focused_leaf)
        {
            focused_survives = true;
            break;
        }
    }
    if (!focused_survives)
    {
        focused_leaf = remote.panes.empty()
            ? kInvalidLeaf
            : local_pane(remote.panes.front().pane_id)
                  .value_or(kInvalidLeaf);
    }
    projection.layout.tree.focused_id = focused_leaf;
    projection.layout.tree.next_leaf_id
        = std::max(maximum_leaf + 1, next_leaf_id_);

    for (const TopologyPane& pane : remote.panes)
    {
        const auto leaf = local_pane(pane.pane_id);
        if (!leaf)
        {
            error = "Server tab pane was not projected.";
            return std::nullopt;
        }
        HostKind host_kind = platform_default_kind;
        if (pane.domain == TopologyPaneDomain::ServerTerminal)
        {
            host_kind = HostKind::RemoteTerminal;
        }
        else if (pane.client_host_kind != "platform_default")
        {
            if (const auto parsed
                = parse_host_kind(pane.client_host_kind))
            {
                host_kind = *parsed;
            }
        }
        projection.layout.panes.push_back({
            .leaf_id = *leaf,
            .launch = {
                .kind = host_kind,
                .remote_terminal_id = pane.terminal_id,
                .client_host_kind
                = pane.domain == TopologyPaneDomain::ClientLocal
                ? pane.client_host_kind
                : std::string{},
            },
            .pane_name = pane.name,
            .pane_id = pane.pane_id,
        });
        projection.pane_names.emplace_back(*leaf, pane.name);
    }
    error.clear();
    return projection;
}

void TopologyProjection::commit_tab(std::string remote_tab_id,
    const TopologyTabProjection& projection)
{
    tab_layout_signatures_[remote_tab_id]
        = projection.structural_signature;
    tab_divider_nodes_[std::move(remote_tab_id)]
        = projection.divider_nodes;
}

std::optional<std::string> TopologyProjection::divider_node(
    std::string_view remote_tab_id, DividerId divider) const
{
    const auto tab = tab_divider_nodes_.find(
        std::string(remote_tab_id));
    if (tab == tab_divider_nodes_.end())
        return std::nullopt;
    const auto node = tab->second.find(divider);
    return node == tab->second.end()
        ? std::nullopt
        : std::optional<std::string>(node->second);
}

bool TopologyProjection::enqueue_command(
    RemoteSessionClient& client, TopologyCommand command,
    std::string_view client_id, std::string& error)
{
    if (command.command_id.empty())
    {
        command.command_id = std::string(client_id) + "-"
            + std::to_string(next_command_serial_++);
    }
    if (!client.enqueue(std::move(command)))
    {
        error = "Shared topology command queue is full.";
        return false;
    }
    error.clear();
    return true;
}

void TopologyProjection::clear_command_activations()
{
    command_activations_.clear();
}

void TopologyProjection::remember_command_activation(
    TopologyCommand command, std::string created_id,
    uint64_t required_revision)
{
    command_activations_.push_back({
        .command = std::move(command),
        .created_id = std::move(created_id),
        .required_revision = required_revision,
    });
}

std::vector<PendingTopologyCommandActivation>
TopologyProjection::take_ready_command_activations(
    uint64_t applied_revision)
{
    std::vector<PendingTopologyCommandActivation> ready;
    std::erase_if(command_activations_,
        [&](PendingTopologyCommandActivation& pending) {
            if (pending.required_revision > applied_revision)
                return false;
            ready.push_back(std::move(pending));
            return true;
        });
    return ready;
}

bool TopologyProjection::announce_apply_error(
    std::string_view error)
{
    if (announced_apply_error_ == error)
        return false;
    announced_apply_error_ = error;
    return true;
}

void TopologyProjection::clear_apply_error()
{
    announced_apply_error_.clear();
}

} // namespace draxul
