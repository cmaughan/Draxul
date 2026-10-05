#pragma once

#include <draxul/agent_model.h>
#include <draxul/host_kind.h>

#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace draxul
{

using LeafId = int;
using DividerId = int;
inline constexpr LeafId kInvalidLeaf = -1;
inline constexpr DividerId kInvalidDivider = -1;

using SpaceId = int;
inline constexpr SpaceId kInvalidSpaceId = -1;
inline constexpr SpaceId kDefaultSpaceId = 0;

// Space, tab, and pane-leaf identifiers are non-negative and strictly below
// this limit, so the follow-on allocator value (id + 1) is always
// representable. An allocator counter may equal the limit, which means its
// identifier space is exhausted and the next allocation must fail.
inline constexpr int kSessionIdentifierLimit = (std::numeric_limits<int>::max)();

constexpr bool valid_session_identifier(int id) noexcept
{
    return id >= 0 && id < kSessionIdentifierLimit;
}

constexpr bool valid_session_counter(int next) noexcept
{
    return next >= 0 && next <= kSessionIdentifierLimit;
}

// Returns the counter's current value and advances it, or nullopt when the
// counter is exhausted or invalid. Never overflows.
constexpr std::optional<int> allocate_session_identifier(int& next) noexcept
{
    if (!valid_session_identifier(next))
        return std::nullopt;
    return next++;
}

enum class SplitDirection
{
    Vertical,
    Horizontal,
};

struct SessionSplitNode
{
    bool is_leaf = true;
    LeafId leaf_id = kInvalidLeaf;
    SplitDirection direction = SplitDirection::Vertical;
    float ratio = 0.5f;
    std::unique_ptr<SessionSplitNode> first;
    std::unique_ptr<SessionSplitNode> second;
};

struct SessionSplitTreeSnapshot
{
    std::unique_ptr<SessionSplitNode> root;
    LeafId focused_id = kInvalidLeaf;
    LeafId next_leaf_id = 0;
};

struct SessionSavedLaunchOptions
{
    HostKind kind = HostKind::Nvim;
    std::string command;
    std::vector<std::string> args;
    std::string working_dir;
    std::string source_path;
    std::vector<std::string> startup_commands;
    std::string remote_terminal_id;
    // Preserves a server topology's client-local host descriptor when it is
    // more specific than, or intentionally aliases, HostKind.
    std::string client_host_kind;
    std::string client_plugin_id;
    std::string client_plugin_config_json;
    // Durable topology relationship for client-local companion panes.
    std::string companion_owner_pane_id;
    std::string pty_capture_file;
};

struct SessionPaneSnapshot
{
    LeafId leaf_id = kInvalidLeaf;
    SessionSavedLaunchOptions launch;
    std::string pane_name;
    std::string pane_id;
    std::optional<AgentIdentity> agent;
    std::optional<AgentSessionRef> agent_session;
    AgentRestorePolicy restore_policy
        = AgentRestorePolicy::ResumeIfAvailable;
};

struct SessionPaneLayoutSnapshot
{
    SessionSplitTreeSnapshot tree;
    std::vector<SessionPaneSnapshot> panes;
    bool zoomed = false;
    LeafId zoomed_leaf = kInvalidLeaf;
};

struct TabSnapshot
{
    int id = -1;
    std::string name;
    bool name_user_set = false;
    SessionPaneLayoutSnapshot pane_layout;
};

struct SpaceSnapshot
{
    SpaceId id = kInvalidSpaceId;
    std::string name;
    std::filesystem::path root_directory;
    int active_tab_id = -1;
    int next_tab_id = 0;
    std::vector<TabSnapshot> tabs;
};

struct SessionSnapshot
{
    int version = 4;
    std::string session_id = "default";
    std::string session_name = "default";
    SpaceId active_space_id = kInvalidSpaceId;
    SpaceId next_space_id = kDefaultSpaceId;
    std::vector<SpaceSnapshot> spaces;
};

struct SessionSummary
{
    std::string session_id;
    std::string session_name;
    int space_count = 0;
    int tab_count = 0;
    int pane_count = 0;
    bool has_saved_state = false;
};

} // namespace draxul
