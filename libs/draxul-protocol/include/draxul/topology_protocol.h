#pragma once

#include <draxul/agent_model.h>

#include <cstddef>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

inline constexpr size_t kTopologyMaxSpaces = 128;
inline constexpr size_t kTopologyMaxTabsPerSpace = 256;
inline constexpr size_t kTopologyMaxPanesPerTab = 256;
inline constexpr size_t kTopologyMaxTextBytes = 4096;

enum class TopologyPaneDomain
{
    ServerTerminal,
    ClientLocal,
};

enum class TopologySplitDirection
{
    Vertical,
    Horizontal,
};

struct TopologyPane
{
    std::string pane_id;
    std::string name;
    TopologyPaneDomain domain = TopologyPaneDomain::ClientLocal;
    std::string terminal_id;
    std::string client_host_kind;
    std::string server_working_directory;
    std::optional<AgentIdentity> agent;
    std::optional<AgentSessionRef> agent_session;
    AgentRestorePolicy restore_policy
        = AgentRestorePolicy::ResumeIfAvailable;

    bool operator==(const TopologyPane&) const = default;
};

struct TopologyNode
{
    std::string node_id;
    bool is_leaf = true;
    std::string pane_id;
    TopologySplitDirection direction = TopologySplitDirection::Vertical;
    float ratio = 0.5f;
    std::string first_node_id;
    std::string second_node_id;

    bool operator==(const TopologyNode&) const = default;
};

struct TopologyTab
{
    std::string tab_id;
    std::string name;
    // Older topology payloads predate this field and therefore represent
    // explicit names. Newly-created default tabs set it to false.
    bool name_user_set = true;
    std::string root_node_id;
    std::vector<TopologyNode> nodes;
    std::vector<TopologyPane> panes;

    bool operator==(const TopologyTab&) const = default;
};

struct TopologySpace
{
    std::string space_id;
    std::string name;
    std::string root_directory;
    std::vector<TopologyTab> tabs;

    bool operator==(const TopologySpace&) const = default;
};

struct TopologySnapshot
{
    uint64_t revision = 0;
    std::string session_id;
    std::vector<TopologySpace> spaces;

    bool operator==(const TopologySnapshot&) const = default;
};

enum class TopologyCommandKind
{
    CreateSpace,
    RenameSpace,
    CloseSpace,
    CreateTab,
    RenameTab,
    CloseTab,
    MoveTab,
    SplitPane,
    ClosePane,
    RenamePane,
    SwapPane,
    RestartPane,
    SetSplitRatio,
    EqualizeSplits,
};

struct TopologyCommand
{
    std::string client_id;
    std::string command_id;
    uint64_t expected_revision = 0;
    TopologyCommandKind kind = TopologyCommandKind::CreateSpace;
    std::string space_id;
    std::string tab_id;
    std::string pane_id;
    std::string target_pane_id;
    std::string node_id;
    std::string name;
    std::string root_directory;
    TopologySplitDirection direction = TopologySplitDirection::Vertical;
    float ratio = 0.5f;
    int move_delta = 0;
    TopologyPaneDomain pane_domain = TopologyPaneDomain::ClientLocal;
    std::string terminal_id;
    std::string client_host_kind;

    bool operator==(const TopologyCommand&) const = default;
};

struct TopologyCommandResult
{
    bool applied = false;
    bool duplicate = false;
    std::string created_id;
    TopologySnapshot snapshot;

    bool operator==(const TopologyCommandResult&) const = default;
};

std::string_view to_string(TopologyPaneDomain domain);
std::optional<TopologyPaneDomain> parse_topology_pane_domain(
    std::string_view value);
std::string_view to_string(TopologySplitDirection direction);
std::optional<TopologySplitDirection> parse_topology_split_direction(
    std::string_view value);
std::string_view to_string(TopologyCommandKind kind);
std::optional<TopologyCommandKind> parse_topology_command_kind(
    std::string_view value);

nlohmann::json topology_snapshot_to_json(const TopologySnapshot& snapshot);
std::optional<TopologySnapshot> topology_snapshot_from_json(
    const nlohmann::json& value, std::string& error);
nlohmann::json topology_command_to_json(const TopologyCommand& command);
std::optional<TopologyCommand> topology_command_from_json(
    const nlohmann::json& value, std::string& error);
nlohmann::json topology_command_result_to_json(
    const TopologyCommandResult& result);
std::optional<TopologyCommandResult> topology_command_result_from_json(
    const nlohmann::json& value, std::string& error);

} // namespace draxul
