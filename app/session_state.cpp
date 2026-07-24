#include "session_state.h"

#include <draxul/config_document.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/toml_support.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <exception>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace draxul
{

namespace
{

constexpr int kSessionStateVersionV1 = 1;
constexpr int kSessionStateVersionV2 = 2;
constexpr size_t kMaxSessionStateBytes = 4 * 1024 * 1024;
constexpr size_t kMaxSpaces = 64;
constexpr size_t kMaxTabsPerSpace = 128;
constexpr size_t kMaxPanesPerTab = 256;
constexpr size_t kMaxTreeDepth = 64;
constexpr size_t kMaxShortTextBytes = 512;
constexpr size_t kMaxCommandTextBytes = 8192;
constexpr size_t kMaxStringListEntries = 256;

struct LegacySessionSnapshotV1
{
    std::string session_id = "default";
    std::string session_name = "default";
    int active_tab_id = -1;
    int next_tab_id = 0;
    std::vector<TabSnapshot> tabs;
};

uint64_t fnv1a_hash(std::string_view text)
{
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : text)
    {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string session_slug(std::string_view session_id)
{
    std::string slug;
    slug.reserve(session_id.size());
    for (unsigned char ch : session_id)
    {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.')
            slug.push_back(static_cast<char>(ch));
        else
            slug.push_back('_');
    }

    if (slug.empty())
        slug = "default";
    if (slug.size() > 48)
        slug.resize(48);
    return slug;
}

std::string session_file_name(std::string_view session_id)
{
    std::ostringstream out;
    out << std::hex << fnv1a_hash(session_id) << "-" << session_slug(session_id) << ".toml";
    return out.str();
}

std::filesystem::path legacy_default_session_state_path()
{
    return ConfigDocument::default_path().parent_path() / "session-state.toml";
}

const char* split_direction_to_string(SplitDirection direction)
{
    switch (direction)
    {
    case SplitDirection::Vertical:
        return "vertical";
    case SplitDirection::Horizontal:
        return "horizontal";
    }
    return "vertical";
}

std::optional<SplitDirection> parse_split_direction(std::string_view value)
{
    if (value == "vertical")
        return SplitDirection::Vertical;
    if (value == "horizontal")
        return SplitDirection::Horizontal;
    return std::nullopt;
}

toml::array make_string_array(const std::vector<std::string>& values)
{
    toml::array array;
    for (const std::string& value : values)
        array.push_back(value);
    return array;
}

toml::table serialize_tree_node(const SplitTree::SnapshotNode& node)
{
    toml::table table;
    if (node.is_leaf)
    {
        table.insert_or_assign("type", "leaf");
        table.insert_or_assign("leaf_id", node.leaf_id);
        return table;
    }

    table.insert_or_assign("type", "split");
    table.insert_or_assign("direction", split_direction_to_string(node.direction));
    table.insert_or_assign("ratio", static_cast<double>(node.ratio));
    if (node.first)
        table.insert_or_assign("first", serialize_tree_node(*node.first));
    if (node.second)
        table.insert_or_assign("second", serialize_tree_node(*node.second));
    return table;
}

std::unique_ptr<SplitTree::SnapshotNode> parse_tree_node(
    const toml::table& table, std::string* error, size_t depth, size_t& node_count)
{
    if (depth > kMaxTreeDepth
        || ++node_count > kMaxPanesPerTab * 2 - 1)
    {
        if (error)
            *error = "Session state layout exceeds structural limits.";
        return nullptr;
    }

    const auto type = toml_support::get_string(table, "type");
    if (!type)
    {
        if (error)
            *error = "Session state is missing a layout node type.";
        return nullptr;
    }

    auto node = std::make_unique<SplitTree::SnapshotNode>();
    if (*type == "leaf")
    {
        const auto leaf_id = toml_support::get_int(table, "leaf_id");
        if (!leaf_id)
        {
            if (error)
                *error = "Session state leaf node is missing leaf_id.";
            return nullptr;
        }
        node->is_leaf = true;
        node->leaf_id = static_cast<LeafId>(*leaf_id);
        return node;
    }

    if (*type != "split")
    {
        if (error)
            *error = "Session state contains an unknown layout node type.";
        return nullptr;
    }

    const auto direction_text = toml_support::get_string(table, "direction");
    const auto ratio_value = toml_support::get_double(table, "ratio");
    const toml::table* first = table["first"].as_table();
    const toml::table* second = table["second"].as_table();
    if (!direction_text || !ratio_value || !first || !second)
    {
        if (error)
            *error = "Session state split node is incomplete.";
        return nullptr;
    }

    const auto direction = parse_split_direction(*direction_text);
    if (!direction)
    {
        if (error)
            *error = "Session state split node uses an unknown direction.";
        return nullptr;
    }

    node->is_leaf = false;
    node->direction = *direction;
    node->ratio = static_cast<float>(*ratio_value);
    node->first = parse_tree_node(*first, error, depth + 1, node_count);
    node->second = parse_tree_node(*second, error, depth + 1, node_count);
    if (!node->first || !node->second)
        return nullptr;
    return node;
}

toml::table serialize_pane_layout(const PaneManager::PaneLayoutSnapshot& state)
{
    toml::table table;
    table.insert_or_assign("focused_leaf", state.tree.focused_id);
    table.insert_or_assign("next_leaf_id", state.tree.next_leaf_id);
    table.insert_or_assign("zoomed", state.zoomed);
    table.insert_or_assign("zoomed_leaf", state.zoomed_leaf);
    if (state.tree.root)
        table.insert_or_assign("layout", serialize_tree_node(*state.tree.root));

    toml::array panes;
    for (const PaneManager::PaneSnapshot& pane : state.panes)
    {
        toml::table pane_table;
        pane_table.insert_or_assign("leaf_id", pane.leaf_id);
        pane_table.insert_or_assign("kind", to_string(pane.launch.kind));
        if (!pane.launch.command.empty())
            pane_table.insert_or_assign("command", pane.launch.command);
        if (!pane.launch.args.empty())
            pane_table.insert_or_assign("args", make_string_array(pane.launch.args));
        if (!pane.launch.working_dir.empty())
            pane_table.insert_or_assign("working_dir", pane.launch.working_dir);
        if (!pane.launch.source_path.empty())
            pane_table.insert_or_assign("source_path", pane.launch.source_path);
        if (!pane.launch.startup_commands.empty())
            pane_table.insert_or_assign(
                "startup_commands", make_string_array(pane.launch.startup_commands));
        if (!pane.pane_name.empty())
            pane_table.insert_or_assign("pane_name", pane.pane_name);
        if (!pane.pane_id.empty())
            pane_table.insert_or_assign("pane_id", pane.pane_id);
        if (pane.agent)
        {
            toml::table agent;
            agent.insert_or_assign("kind", pane.agent->kind);
            agent.insert_or_assign("display_name", pane.agent->display_name);
            agent.insert_or_assign("instance_id", pane.agent->instance_id);
            pane_table.insert_or_assign("agent", std::move(agent));
        }
        panes.push_back(std::move(pane_table));
    }
    table.insert_or_assign("panes", std::move(panes));
    return table;
}

std::optional<PaneManager::PaneLayoutSnapshot> parse_pane_layout(
    const toml::table& table, std::string* error)
{
    PaneManager::PaneLayoutSnapshot state;
    const auto focused_leaf = toml_support::get_int(table, "focused_leaf");
    const auto next_leaf_id = toml_support::get_int(table, "next_leaf_id");
    const auto zoomed = toml_support::get_bool(table, "zoomed");
    const auto zoomed_leaf = toml_support::get_int(table, "zoomed_leaf");
    const toml::table* layout = table["layout"].as_table();
    const toml::array* panes = table["panes"].as_array();
    if (!focused_leaf || !next_leaf_id || !zoomed || !zoomed_leaf || !layout || !panes)
    {
        if (error)
            *error = "Session state tab is missing layout metadata.";
        return std::nullopt;
    }

    state.tree.focused_id = static_cast<LeafId>(*focused_leaf);
    state.tree.next_leaf_id = static_cast<LeafId>(*next_leaf_id);
    size_t tree_node_count = 0;
    state.tree.root = parse_tree_node(*layout, error, 0, tree_node_count);
    if (!state.tree.root)
        return std::nullopt;

    state.zoomed = *zoomed;
    state.zoomed_leaf = static_cast<LeafId>(*zoomed_leaf);

    if (panes->size() > kMaxPanesPerTab)
    {
        if (error)
            *error = "Session state tab exceeds the pane limit.";
        return std::nullopt;
    }

    for (const toml::node& node : *panes)
    {
        const toml::table* pane_table = node.as_table();
        if (!pane_table)
        {
            if (error)
                *error = "Session state pane entry is not a table.";
            return std::nullopt;
        }

        const auto leaf_id = toml_support::get_int(*pane_table, "leaf_id");
        const auto kind_text = toml_support::get_string(*pane_table, "kind");
        if (!leaf_id || !kind_text)
        {
            if (error)
                *error = "Session state pane is missing required fields.";
            return std::nullopt;
        }

        const auto kind = parse_host_kind(*kind_text);
        if (!kind)
        {
            if (error)
                *error = "Session state pane uses an unknown host kind.";
            return std::nullopt;
        }

        PaneManager::PaneSnapshot pane;
        pane.leaf_id = static_cast<LeafId>(*leaf_id);
        pane.launch.kind = *kind;
        pane.launch.command = toml_support::get_string(*pane_table, "command").value_or("");
        pane.launch.args = toml_support::get_string_array(*pane_table, "args").value_or(
            std::vector<std::string>{});
        pane.launch.working_dir = toml_support::get_string(*pane_table, "working_dir").value_or("");
        pane.launch.source_path = toml_support::get_string(*pane_table, "source_path").value_or("");
        pane.launch.startup_commands = toml_support::get_string_array(
            *pane_table, "startup_commands")
                                         .value_or(std::vector<std::string>{});
        pane.pane_name = toml_support::get_string(*pane_table, "pane_name").value_or("");
        pane.pane_id = toml_support::get_string(*pane_table, "pane_id").value_or(
            "pane-" + std::to_string(static_cast<int>(pane.leaf_id)));
        if (const toml::table* agent = (*pane_table)["agent"].as_table())
        {
            const auto kind = toml_support::get_string(*agent, "kind");
            const auto display_name = toml_support::get_string(*agent, "display_name");
            const auto instance_id = toml_support::get_string(*agent, "instance_id");
            if (!kind || kind->empty() || !display_name || display_name->empty()
                || !instance_id || instance_id->empty())
            {
                if (error)
                    *error = "Session state agent identity is incomplete.";
                return std::nullopt;
            }
            pane.agent = AgentIdentity{
                .kind = *kind,
                .display_name = *display_name,
                .instance_id = *instance_id,
            };
        }
        state.panes.push_back(std::move(pane));
    }

    return state;
}

toml::table serialize_tab(const TabSnapshot& tab)
{
    toml::table table;
    table.insert_or_assign("id", tab.id);
    if (!tab.name.empty())
        table.insert_or_assign("name", tab.name);
    table.insert_or_assign("name_user_set", tab.name_user_set);
    table.insert_or_assign("pane_layout", serialize_pane_layout(tab.pane_layout));
    return table;
}

std::optional<TabSnapshot> parse_tab(
    const toml::table& table, std::string_view pane_layout_key, std::string* error)
{
    const auto id = toml_support::get_int(table, "id");
    const auto name_user_set = toml_support::get_bool(table, "name_user_set");
    const toml::table* pane_layout = table[pane_layout_key].as_table();
    if (!id || !name_user_set || !pane_layout)
    {
        if (error)
            *error = "Session state tab is missing required fields.";
        return std::nullopt;
    }

    TabSnapshot tab;
    tab.id = static_cast<int>(*id);
    tab.name = toml_support::get_string(table, "name").value_or("");
    tab.name_user_set = *name_user_set;
    auto parsed_pane_layout = parse_pane_layout(*pane_layout, error);
    if (!parsed_pane_layout)
        return std::nullopt;
    tab.pane_layout = std::move(*parsed_pane_layout);
    return tab;
}

bool collect_tree_leaf_ids(const SplitTree::SnapshotNode& node,
    std::unordered_set<LeafId>& leaf_ids, std::string* error,
    size_t depth, size_t& node_count)
{
    if (depth > kMaxTreeDepth
        || ++node_count > kMaxPanesPerTab * 2 - 1)
    {
        if (error)
            *error = "Session state layout exceeds structural limits.";
        return false;
    }

    if (node.is_leaf)
    {
        if (node.leaf_id == kInvalidLeaf)
        {
            if (error)
                *error = "Session state contains an invalid pane id.";
            return false;
        }
        if (!leaf_ids.insert(node.leaf_id).second)
        {
            if (error)
                *error = "Session state contains a duplicate pane id.";
            return false;
        }
        return true;
    }

    if (!node.first || !node.second)
    {
        if (error)
            *error = "Session state split node is incomplete.";
        return false;
    }
    return collect_tree_leaf_ids(*node.first, leaf_ids, error, depth + 1, node_count)
        && collect_tree_leaf_ids(*node.second, leaf_ids, error, depth + 1, node_count);
}

bool validate_text_limit(
    std::string_view text, size_t limit, std::string_view field, std::string* error)
{
    if (text.size() <= limit)
        return true;
    if (error)
        *error = "Session state " + std::string(field) + " exceeds the text limit.";
    return false;
}

bool validate_string_list(const std::vector<std::string>& values,
    std::string_view field, std::string* error)
{
    if (values.size() > kMaxStringListEntries)
    {
        if (error)
            *error = "Session state " + std::string(field) + " exceeds the entry limit.";
        return false;
    }
    for (const std::string& value : values)
    {
        if (!validate_text_limit(value, kMaxCommandTextBytes, field, error))
            return false;
    }
    return true;
}

bool validate_tab_snapshots(const std::vector<TabSnapshot>& tabs, std::string* error)
{
    if (tabs.size() > kMaxTabsPerSpace)
    {
        if (error)
            *error = "Session state Space exceeds the tab limit.";
        return false;
    }

    std::unordered_set<int> tab_ids;
    for (const TabSnapshot& tab : tabs)
    {
        if (!validate_text_limit(tab.name, kMaxShortTextBytes, "tab name", error))
            return false;
        if (tab.id < 0)
        {
            if (error)
                *error = "Session state contains an invalid tab id.";
            return false;
        }
        if (!tab_ids.insert(tab.id).second)
        {
            if (error)
                *error = "Session state contains a duplicate tab id.";
            return false;
        }
        if (!tab.pane_layout.tree.root)
        {
            if (error)
                *error = "Session state tab is missing a layout tree.";
            return false;
        }
        if (tab.pane_layout.panes.size() > kMaxPanesPerTab)
        {
            if (error)
                *error = "Session state tab exceeds the pane limit.";
            return false;
        }

        std::unordered_set<LeafId> tree_leaf_ids;
        size_t tree_node_count = 0;
        if (!collect_tree_leaf_ids(
                *tab.pane_layout.tree.root, tree_leaf_ids, error, 0, tree_node_count))
            return false;

        std::unordered_set<LeafId> pane_leaf_ids;
        std::unordered_set<std::string> stable_pane_ids;
        for (const PaneManager::PaneSnapshot& pane : tab.pane_layout.panes)
        {
            if (!validate_text_limit(
                    pane.pane_name, kMaxShortTextBytes, "pane name", error)
                || !validate_text_limit(
                    pane.pane_id, kMaxShortTextBytes, "pane id", error)
                || !validate_text_limit(
                    pane.launch.command, kMaxCommandTextBytes, "host command", error)
                || !validate_text_limit(
                    pane.launch.working_dir, kMaxCommandTextBytes, "working directory", error)
                || !validate_text_limit(
                    pane.launch.source_path, kMaxCommandTextBytes, "source path", error)
                || !validate_text_limit(
                    pane.launch.pty_capture_file, kMaxCommandTextBytes, "capture path", error)
                || !validate_string_list(pane.launch.args, "host arguments", error)
                || !validate_string_list(
                    pane.launch.startup_commands, "startup commands", error))
            {
                return false;
            }
            if (pane.agent
                && (pane.agent->kind.empty() || pane.agent->display_name.empty()
                    || pane.agent->instance_id.empty()))
            {
                if (error)
                    *error = "Session state agent identity is incomplete.";
                return false;
            }
            if (pane.agent
                && (!validate_text_limit(
                        pane.agent->kind, kMaxShortTextBytes, "agent kind", error)
                    || !validate_text_limit(pane.agent->display_name,
                        kMaxShortTextBytes, "agent display name", error)
                    || !validate_text_limit(pane.agent->instance_id,
                        kMaxShortTextBytes, "agent instance id", error)))
            {
                return false;
            }
            if (pane.leaf_id == kInvalidLeaf || !pane_leaf_ids.insert(pane.leaf_id).second)
            {
                if (error)
                    *error = "Session state contains a duplicate or invalid pane entry.";
                return false;
            }
            if (!pane.pane_id.empty() && !stable_pane_ids.insert(pane.pane_id).second)
            {
                if (error)
                    *error = "Session state contains a duplicate stable pane id.";
                return false;
            }
        }
        if (tree_leaf_ids != pane_leaf_ids)
        {
            if (error)
                *error = "Session state pane entries do not match the layout tree.";
            return false;
        }
    }

    return true;
}

bool validate_session_snapshot_impl(const SessionSnapshot& state, std::string* error)
{
    if (state.version != kSessionStateVersionV2)
    {
        if (error)
            *error = "Unsupported session state version.";
        return false;
    }
    if (state.spaces.size() > kMaxSpaces)
    {
        if (error)
            *error = "Session state exceeds the Space limit.";
        return false;
    }
    if (!validate_text_limit(
            state.session_id, kMaxShortTextBytes, "session id", error)
        || !validate_text_limit(
            state.session_name, kMaxShortTextBytes, "session name", error))
    {
        return false;
    }

    std::unordered_set<SpaceId> space_ids;
    std::unordered_set<std::string> agent_instance_ids;
    for (const SpaceSnapshot& space : state.spaces)
    {
        if (!validate_text_limit(space.name, kMaxShortTextBytes, "Space name", error))
            return false;
        if (space.root_directory.native().size() > kMaxCommandTextBytes)
        {
            if (error)
                *error = "Session state root directory exceeds the text limit.";
            return false;
        }
        if (space.id == kInvalidSpaceId || !space_ids.insert(space.id).second)
        {
            if (error)
                *error = "Session state contains a duplicate or invalid Space id.";
            return false;
        }
        if (!validate_tab_snapshots(space.tabs, error))
            return false;
        for (const TabSnapshot& tab : space.tabs)
        {
            for (const PaneManager::PaneSnapshot& pane : tab.pane_layout.panes)
            {
                if (pane.agent
                    && !agent_instance_ids.insert(pane.agent->instance_id).second)
                {
                    if (error)
                        *error = "Session state contains a duplicate agent instance id.";
                    return false;
                }
            }
        }
    }

    if (error)
        error->clear();
    return true;
}

SessionSnapshot migrate_v1_to_v2(LegacySessionSnapshotV1 legacy)
{
    SessionSnapshot state;
    state.version = kSessionStateVersionV2;
    state.session_id = std::move(legacy.session_id);
    state.session_name = std::move(legacy.session_name);
    state.active_space_id = kDefaultSpaceId;
    state.next_space_id = kDefaultSpaceId + 1;

    SpaceSnapshot space;
    space.id = kDefaultSpaceId;
    space.name = "default";
    space.active_tab_id = legacy.active_tab_id;
    space.next_tab_id = legacy.next_tab_id;
    space.tabs = std::move(legacy.tabs);
    state.spaces.push_back(std::move(space));
    return state;
}

std::optional<SessionSnapshot> decode_v1_document(
    const toml::table& document, std::string* error)
{
    LegacySessionSnapshotV1 legacy;
    legacy.session_id = toml_support::get_string(document, "session_id").value_or("default");
    legacy.session_name = toml_support::get_string(document, "session_name").value_or(
        legacy.session_id);
    // Version 1 called Draxul tabs "workspaces". Keep those wire keys stable
    // while the in-memory vocabulary moves to Session -> Tab.
    legacy.active_tab_id = static_cast<int>(
        toml_support::get_int(document, "active_workspace_id").value_or(-1));
    legacy.next_tab_id = static_cast<int>(
        toml_support::get_int(document, "next_workspace_id").value_or(0));

    const toml::array* tabs = document["workspaces"].as_array();
    if (!tabs)
    {
        if (error)
            *error = "Session state is missing tabs.";
        return std::nullopt;
    }
    if (tabs->size() > kMaxTabsPerSpace)
    {
        if (error)
            *error = "Session state Space exceeds the tab limit.";
        return std::nullopt;
    }

    for (const toml::node& node : *tabs)
    {
        const toml::table* tab_table = node.as_table();
        if (!tab_table)
        {
            if (error)
                *error = "Session state tab entry is not a table.";
            return std::nullopt;
        }

        // Version 1 persisted this table as "host_manager". Keep the wire key
        // stable while the in-memory owner is renamed to PaneManager.
        auto tab = parse_tab(*tab_table, "host_manager", error);
        if (!tab)
            return std::nullopt;
        legacy.tabs.push_back(std::move(*tab));
    }

    if (!validate_tab_snapshots(legacy.tabs, error))
        return std::nullopt;
    SessionSnapshot state = migrate_v1_to_v2(std::move(legacy));
    if (!validate_session_snapshot_impl(state, error))
        return std::nullopt;
    return state;
}

std::optional<SessionSnapshot> decode_v2_document(
    const toml::table& document, std::string* error)
{
    SessionSnapshot state;
    state.version = kSessionStateVersionV2;
    state.session_id = toml_support::get_string(document, "session_id").value_or("default");
    state.session_name = toml_support::get_string(document, "session_name").value_or(
        state.session_id);
    state.active_space_id = static_cast<SpaceId>(
        toml_support::get_int(document, "active_space_id").value_or(kInvalidSpaceId));
    state.next_space_id = static_cast<SpaceId>(
        toml_support::get_int(document, "next_space_id").value_or(kDefaultSpaceId));

    const toml::array* spaces = document["spaces"].as_array();
    if (!spaces)
    {
        if (error)
            *error = "Session state is missing Spaces.";
        return std::nullopt;
    }
    if (spaces->size() > kMaxSpaces)
    {
        if (error)
            *error = "Session state exceeds the Space limit.";
        return std::nullopt;
    }

    for (const toml::node& node : *spaces)
    {
        const toml::table* space_table = node.as_table();
        if (!space_table)
        {
            if (error)
                *error = "Session state Space entry is not a table.";
            return std::nullopt;
        }

        const auto id = toml_support::get_int(*space_table, "id");
        const toml::array* tabs = (*space_table)["tabs"].as_array();
        if (!id || !tabs)
        {
            if (error)
                *error = "Session state Space is missing required fields.";
            return std::nullopt;
        }
        if (tabs->size() > kMaxTabsPerSpace)
        {
            if (error)
                *error = "Session state Space exceeds the tab limit.";
            return std::nullopt;
        }

        SpaceSnapshot space;
        space.id = static_cast<SpaceId>(*id);
        space.name = toml_support::get_string(*space_table, "name").value_or("default");
        space.root_directory = toml_support::get_string(
            *space_table, "root_directory").value_or("");
        space.active_tab_id = static_cast<int>(
            toml_support::get_int(*space_table, "active_tab_id").value_or(-1));
        space.next_tab_id = static_cast<int>(
            toml_support::get_int(*space_table, "next_tab_id").value_or(0));

        for (const toml::node& tab_node : *tabs)
        {
            const toml::table* tab_table = tab_node.as_table();
            if (!tab_table)
            {
                if (error)
                    *error = "Session state tab entry is not a table.";
                return std::nullopt;
            }
            auto tab = parse_tab(*tab_table, "pane_layout", error);
            if (!tab)
                return std::nullopt;
            space.tabs.push_back(std::move(*tab));
        }
        state.spaces.push_back(std::move(space));
    }

    if (!validate_session_snapshot_impl(state, error))
        return std::nullopt;
    return state;
}

std::optional<SessionSnapshot> load_session_state_from_path(
    const std::filesystem::path& path, std::string* error)
{
    if (!std::filesystem::exists(path))
        return std::nullopt;

    std::error_code size_error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, size_error);
    if (size_error)
    {
        if (error)
            *error = "Unable to inspect session state.";
        return std::nullopt;
    }
    if (file_size > kMaxSessionStateBytes)
    {
        if (error)
            *error = "Session state exceeds the file size limit.";
        return std::nullopt;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        if (error)
            *error = "Unable to open session state for reading.";
        return std::nullopt;
    }
    const std::string content{
        std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{}
    };
    return decode_session_state(content, error);
}

SessionSummary summarize_session_state(const SessionSnapshot& state)
{
    SessionSummary summary;
    summary.session_id = state.session_id;
    summary.session_name = state.session_name;
    summary.space_count = static_cast<int>(state.spaces.size());
    summary.has_saved_state = true;
    for (const SpaceSnapshot& space : state.spaces)
    {
        summary.tab_count += static_cast<int>(space.tabs.size());
        for (const TabSnapshot& tab : space.tabs)
            summary.pane_count += static_cast<int>(tab.pane_layout.panes.size());
    }
    return summary;
}

} // namespace

bool validate_session_snapshot(const SessionSnapshot& state, std::string* error)
{
    return validate_session_snapshot_impl(state, error);
}

std::optional<SessionSnapshot> decode_session_state(
    std::string_view content, std::string* error)
{
    if (content.size() > kMaxSessionStateBytes)
    {
        if (error)
            *error = "Session state exceeds the file size limit.";
        return std::nullopt;
    }

    std::string parse_error;
    auto document = toml_support::parse_document(content, &parse_error);
    if (!document)
    {
        if (error)
            *error = "Session state TOML could not be parsed.";
        return std::nullopt;
    }

    const auto version = toml_support::get_int(*document, "version");
    if (!version)
    {
        if (error)
            *error = "Session state is missing a version.";
        return std::nullopt;
    }

    switch (*version)
    {
    case kSessionStateVersionV1:
        return decode_v1_document(*document, error);
    case kSessionStateVersionV2:
        return decode_v2_document(*document, error);
    default:
        if (error)
            *error = "Unsupported session state version.";
        return std::nullopt;
    }
}

std::optional<std::string> encode_session_state(
    const SessionSnapshot& state, std::string* error)
{
    if (!validate_session_snapshot(state, error))
        return std::nullopt;

    try
    {
        const std::string normalized_id = state.session_id.empty() ? "default" : state.session_id;
        toml::table document;
        document.insert_or_assign("version", kSessionStateVersionV2);
        document.insert_or_assign("session_id", normalized_id);
        document.insert_or_assign(
            "session_name", state.session_name.empty() ? normalized_id : state.session_name);
        document.insert_or_assign("active_space_id", state.active_space_id);
        document.insert_or_assign("next_space_id", state.next_space_id);

        toml::array spaces;
        for (const SpaceSnapshot& space : state.spaces)
        {
            toml::table space_table;
            space_table.insert_or_assign("id", space.id);
            if (!space.name.empty())
                space_table.insert_or_assign("name", space.name);
            if (!space.root_directory.empty())
            {
                space_table.insert_or_assign(
                    "root_directory", space.root_directory.generic_string());
            }
            space_table.insert_or_assign("active_tab_id", space.active_tab_id);
            space_table.insert_or_assign("next_tab_id", space.next_tab_id);

            toml::array tabs;
            for (const TabSnapshot& tab : space.tabs)
                tabs.push_back(serialize_tab(tab));
            space_table.insert_or_assign("tabs", std::move(tabs));
            spaces.push_back(std::move(space_table));
        }
        document.insert_or_assign("spaces", std::move(spaces));

        std::ostringstream out;
        out << document << '\n';
        std::string content = out.str();
        if (content.size() > kMaxSessionStateBytes)
        {
            if (error)
                *error = "Session state exceeds the file size limit.";
            return std::nullopt;
        }
        if (error)
            error->clear();
        return content;
    }
    catch (const std::exception&)
    {
        if (error)
            *error = "Unable to encode session state.";
        return std::nullopt;
    }
}

std::filesystem::path session_state_directory()
{
    PERF_MEASURE();
    return ConfigDocument::default_path().parent_path() / "sessions";
}

std::filesystem::path session_state_path(std::string_view session_id)
{
    PERF_MEASURE();
    const std::string normalized_id = session_id.empty() ? "default" : std::string(session_id);
    return session_state_directory() / session_file_name(normalized_id);
}

bool has_saved_session_state(std::string_view session_id, std::string* error)
{
    PERF_MEASURE();
    std::string load_error;
    const bool exists = load_session_state(session_id, &load_error).has_value();
    if (!load_error.empty())
    {
        if (error)
            *error = load_error;
        return false;
    }
    if (error)
        error->clear();
    return exists;
}

static bool replace_session_state_file(const std::filesystem::path& temporary,
    const std::filesystem::path& destination, std::string* error)
{
#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        return true;
    }
    const DWORD replace_error = GetLastError();
    if (error)
    {
        *error = "Unable to replace session state: "
            + std::system_category().message(static_cast<int>(replace_error));
    }
    return false;
#else
    std::error_code ec;
    std::filesystem::rename(temporary, destination, ec);
    if (!ec)
        return true;
    if (error)
        *error = "Unable to replace session state: " + ec.message();
    return false;
#endif
}

bool save_session_state_to_path(const SessionSnapshot& state,
    const std::filesystem::path& path, std::string* error)
{
    PERF_MEASURE();
    try
    {
        auto encoded = encode_session_state(state, error);
        if (!encoded)
            return false;

        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        std::filesystem::path temporary = path;
        temporary += ".tmp";
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            if (error)
                *error = "Unable to open temporary session state for writing.";
            return false;
        }
        out << *encoded;
        out.flush();
        if (!out)
        {
            if (error)
                *error = "Failed writing session state.";
            out.close();
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
            return false;
        }
        out.close();
        if (!replace_session_state_file(temporary, path, error))
        {
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
            return false;
        }
        if (error)
            error->clear();
        return true;
    }
    catch (const std::exception&)
    {
        if (error)
            *error = "Unable to save session state.";
        return false;
    }
}

bool save_session_state(const SessionSnapshot& state, std::string* error)
{
    const std::string normalized_id = state.session_id.empty() ? "default" : state.session_id;
    return save_session_state_to_path(
        state, session_state_path(normalized_id), error);
}

bool delete_session_state(std::string_view session_id, std::string* error)
{
    PERF_MEASURE();
    try
    {
        const std::string normalized_id = session_id.empty() ? "default" : std::string(session_id);
        std::error_code ec;
        const bool removed = std::filesystem::remove(session_state_path(normalized_id), ec);
        if (ec)
        {
            if (error)
                *error = ec.message();
            return false;
        }

        if (normalized_id == "default")
        {
            std::filesystem::remove(legacy_default_session_state_path(), ec);
            if (ec)
            {
                if (error)
                    *error = ec.message();
                return false;
            }
        }

        return removed || normalized_id == "default";
    }
    catch (const std::exception& ex)
    {
        if (error)
            *error = ex.what();
        return false;
    }
}

std::optional<SessionSnapshot> load_session_state(
    std::string_view session_id, std::string* error)
{
    PERF_MEASURE();
    const std::string normalized_id = session_id.empty() ? "default" : std::string(session_id);
    const auto path = session_state_path(normalized_id);
    if (auto state = load_session_state_from_path(path, error))
        return state;

    if (normalized_id == "default")
        return load_session_state_from_path(legacy_default_session_state_path(), error);

    return std::nullopt;
}

std::optional<SessionSnapshot> load_session_state(std::string* error)
{
    return load_session_state("default", error);
}

std::vector<SessionSummary> list_saved_sessions(std::string* error)
{
    PERF_MEASURE();
    std::vector<SessionSummary> sessions;
    try
    {
        std::unordered_map<std::string, SessionSummary> by_id;
        const auto dir = session_state_directory();
        if (std::filesystem::exists(dir))
        {
            for (const auto& entry : std::filesystem::directory_iterator(dir))
            {
                if (!entry.is_regular_file() || entry.path().extension() != ".toml")
                    continue;

                const std::string file_name = entry.path().filename().string();
                std::string load_error;
                if (file_name.ends_with(".meta.toml"))
                    continue;
                if (auto state = load_session_state_from_path(entry.path(), &load_error))
                {
                    SessionSummary summary = summarize_session_state(*state);
                    by_id[summary.session_id] = std::move(summary);
                }
                else if (!load_error.empty())
                {
                    DRAXUL_LOG_WARN(LogCategory::App,
                        "Skipping invalid session state %s: %s",
                        entry.path().string().c_str(), load_error.c_str());
                }
            }
        }

        const auto legacy_path = legacy_default_session_state_path();
        const bool have_default = by_id.contains("default");
        if (!have_default)
        {
            std::string load_error;
            if (auto legacy = load_session_state_from_path(legacy_path, &load_error))
            {
                SessionSummary summary = summarize_session_state(*legacy);
                by_id[summary.session_id] = std::move(summary);
            }
        }

        sessions.reserve(by_id.size());
        for (auto& [id, summary] : by_id)
            sessions.push_back(std::move(summary));

        std::sort(sessions.begin(), sessions.end(), [](const SessionSummary& lhs, const SessionSummary& rhs) {
            return lhs.session_id < rhs.session_id;
        });
    }
    catch (const std::exception& ex)
    {
        if (error)
            *error = ex.what();
    }
    return sessions;
}

} // namespace draxul
