#pragma once

#include <draxul/session_model.h>
#include <draxul/topology_protocol.h>

#include <charconv>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace draxul
{

// Extracts the trailing "-<number>" serial from a topology identifier
// ("pane-12" -> 12). Returns `fallback` when the id has no parseable,
// non-negative serial. The integral type is caller-chosen because the two
// historical copies disagreed: the session bridge assigns `int` tab/leaf ids
// with a moving sentinel fallback, while the topology service tracks
// `uint64_t` allocation serials with fallback 0.
template <typename T>
T topology_id_serial(std::string_view value, T fallback)
{
    static_assert(std::is_integral_v<T>);
    const size_t separator = value.find_last_of('-');
    if (separator == std::string_view::npos
        || separator + 1 >= value.size())
    {
        return fallback;
    }
    T parsed{};
    const char* begin = value.data() + separator + 1;
    const char* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end)
        return fallback;
    if constexpr (std::is_signed_v<T>)
    {
        if (parsed < 0)
            return fallback;
    }
    return parsed;
}

// Topology traversal by durable identity. These are the single canonical
// lookups shared by the server's topology service, the session bridge, and
// the topology CLI's index-tracking wrappers.
const TopologySpace* find_space(
    const TopologySnapshot& snapshot, std::string_view space_id);
TopologySpace* find_space(
    TopologySnapshot& snapshot, std::string_view space_id);
const TopologyTab* find_tab(
    const TopologySpace& space, std::string_view tab_id);
TopologyTab* find_tab(TopologySpace& space, std::string_view tab_id);
const TopologyPane* find_pane(
    const TopologyTab& tab, std::string_view pane_id);
TopologyPane* find_pane(TopologyTab& tab, std::string_view pane_id);
const TopologyNode* find_node(
    const TopologyTab& tab, std::string_view node_id);
TopologyNode* find_node(TopologyTab& tab, std::string_view node_id);

struct TopologyTabLayoutOptions
{
    // Resolves the HostKind recorded for a client-local pane whose
    // client_host_kind is empty, unparseable, or the
    // kTopologyPlatformDefaultHostKind sentinel. The server session bridge
    // passes HostKind::Nvim as an inert placeholder (the preserved
    // client_host_kind string stays authoritative when the snapshot is
    // restored); the client projection passes the local platform's default
    // shell so the pane is immediately launchable.
    HostKind fallback_host_kind = HostKind::Nvim;
    // Assigns the session LeafId for each topology leaf node. Called exactly
    // once per leaf in depth-first first-then-second traversal order. The
    // server bridge derives ids from the node id's numeric suffix (durable
    // across captures); the client projection allocates monotonically from
    // its persistent pane->leaf map (stable across topology revisions).
    std::function<LeafId(const TopologyNode& leaf, const TopologyPane& pane)>
        allocate_leaf;
};

struct TopologyTabLayout
{
    // tree.root, tree.next_leaf_id (= maximum_leaf + 1), and panes are
    // filled; tree.focused_id is left at kInvalidLeaf because focus policy
    // is caller-specific (the bridge focuses the first pane, the projection
    // preserves the locally focused leaf when it survives).
    SessionPaneLayoutSnapshot layout;
    std::unordered_map<std::string, LeafId> leaf_by_pane;
    // Internal (split) node ids in depth-first pre-order; the client
    // projection maps these to DividerIds by index.
    std::vector<std::string> split_node_ids;
    LeafId maximum_leaf = kInvalidLeaf;
};

// The one conversion from a TopologyTab's node/pane graph to the durable
// SessionSplitNode/SessionPaneSnapshot layout, shared by the server's
// session capture and the client's projection so the two sides cannot
// disagree about topology. The pane mapping carries the full agent identity
// (agent, agent_session, restore_policy).
std::optional<TopologyTabLayout> topology_tab_to_layout(
    const TopologyTab& tab, const TopologyTabLayoutOptions& options,
    std::string& error);

// Canonical leaf allocator for durable captures: derives each LeafId from
// the topology node id's numeric suffix, consuming one fallback serial per
// leaf whether or not it is used.
std::function<LeafId(const TopologyNode&, const TopologyPane&)>
make_node_serial_leaf_allocator(int fallback_start = 1'000'000);

} // namespace draxul
