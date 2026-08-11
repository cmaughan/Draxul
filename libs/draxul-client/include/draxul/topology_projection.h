#pragma once

#include <draxul/session_model.h>
#include <draxul/topology_protocol.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace draxul
{

class RemoteSessionClient;

struct TopologyTabProjection
{
    bool requires_reconcile = true;
    SessionPaneLayoutSnapshot layout;
    std::vector<std::pair<LeafId, std::string>> pane_names;
    std::string structural_signature;
    std::unordered_map<DividerId, std::string> divider_nodes;
};

struct PendingTopologyCommandActivation
{
    TopologyCommand command;
    std::string created_id;
    uint64_t required_revision = 0;
};

// Renderer-free state machine that translates server topology identities and
// split descriptors into the durable pane-layout values consumed by an App
// adapter. It deliberately knows nothing about windows, renderers,
// SpaceController, TabController, or PaneManager.
class TopologyProjection
{
public:
    bool empty() const noexcept;

    void bind_space(std::string remote_id, SpaceId local_id);
    std::optional<SpaceId> local_space(
        std::string_view remote_id) const;
    std::optional<std::string> remote_space(
        SpaceId local_id) const;
    void erase_space(std::string_view remote_id);
    const std::unordered_map<std::string, SpaceId>& spaces() const noexcept;

    void bind_tab(
        std::string remote_id, SpaceId local_space_id, int local_tab_id);
    std::optional<std::pair<SpaceId, int>> local_tab(
        std::string_view remote_id) const;
    void erase_tab(std::string_view remote_id);
    const std::unordered_map<std::string, std::pair<SpaceId, int>>&
    tabs() const noexcept;

    std::optional<LeafId> local_pane(
        std::string_view remote_id) const;
    void prune_panes(
        const std::unordered_set<std::string>& live_remote_ids);

    std::optional<TopologyTabProjection> project_tab(
        const TopologyTab& remote, LeafId focused_leaf,
        HostKind platform_default_kind, std::string& error);
    void commit_tab(std::string remote_tab_id,
        const TopologyTabProjection& projection);
    std::optional<std::string> divider_node(
        std::string_view remote_tab_id, DividerId divider) const;

    bool enqueue_command(RemoteSessionClient& client,
        TopologyCommand command, std::string_view client_id,
        std::string& error);

    void clear_command_activations();
    void remember_command_activation(TopologyCommand command,
        std::string created_id, uint64_t required_revision);
    std::vector<PendingTopologyCommandActivation>
    take_ready_command_activations(uint64_t applied_revision);

    bool announce_apply_error(std::string_view error);
    void clear_apply_error();

private:
    std::unordered_map<std::string, SpaceId> space_to_local_;
    std::unordered_map<SpaceId, std::string> local_to_space_;
    std::unordered_map<std::string, std::pair<SpaceId, int>>
        tab_to_local_;
    std::unordered_map<std::string, LeafId> pane_to_leaf_;
    std::unordered_map<std::string, std::string>
        tab_layout_signatures_;
    std::unordered_map<std::string,
        std::unordered_map<DividerId, std::string>>
        tab_divider_nodes_;
    std::vector<PendingTopologyCommandActivation>
        command_activations_;
    LeafId next_leaf_id_ = 0;
    uint64_t next_command_serial_ = 1;
    std::string announced_apply_error_;
};

} // namespace draxul
