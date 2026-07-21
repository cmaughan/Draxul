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

namespace draxul
{

namespace
{

constexpr int kSessionStateVersion = 1;

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
    const toml::table& table, std::string* error)
{
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
    node->first = parse_tree_node(*first, error);
    node->second = parse_tree_node(*second, error);
    if (!node->first || !node->second)
        return nullptr;
    return node;
}

toml::table serialize_host_manager_state(const HostManager::SessionState& state)
{
    toml::table table;
    table.insert_or_assign("focused_leaf", state.tree.focused_id);
    table.insert_or_assign("next_leaf_id", state.tree.next_leaf_id);
    table.insert_or_assign("zoomed", state.zoomed);
    table.insert_or_assign("zoomed_leaf", state.zoomed_leaf);
    if (state.tree.root)
        table.insert_or_assign("layout", serialize_tree_node(*state.tree.root));

    toml::array panes;
    for (const HostManager::PaneSessionState& pane : state.panes)
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
        panes.push_back(std::move(pane_table));
    }
    table.insert_or_assign("panes", std::move(panes));
    return table;
}

std::optional<HostManager::SessionState> parse_host_manager_state(
    const toml::table& table, std::string* error)
{
    HostManager::SessionState state;
    const auto focused_leaf = toml_support::get_int(table, "focused_leaf");
    const auto next_leaf_id = toml_support::get_int(table, "next_leaf_id");
    const auto zoomed = toml_support::get_bool(table, "zoomed");
    const auto zoomed_leaf = toml_support::get_int(table, "zoomed_leaf");
    const toml::table* layout = table["layout"].as_table();
    const toml::array* panes = table["panes"].as_array();
    if (!focused_leaf || !next_leaf_id || !zoomed || !zoomed_leaf || !layout || !panes)
    {
        if (error)
            *error = "Session state workspace is missing layout metadata.";
        return std::nullopt;
    }

    state.tree.focused_id = static_cast<LeafId>(*focused_leaf);
    state.tree.next_leaf_id = static_cast<LeafId>(*next_leaf_id);
    state.tree.root = parse_tree_node(*layout, error);
    if (!state.tree.root)
        return std::nullopt;

    state.zoomed = *zoomed;
    state.zoomed_leaf = static_cast<LeafId>(*zoomed_leaf);

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

        HostManager::PaneSessionState pane;
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
        state.panes.push_back(std::move(pane));
    }

    return state;
}

std::optional<AppSessionState> load_session_state_from_path(
    const std::filesystem::path& path, std::string* error)
{
    if (!std::filesystem::exists(path))
        return std::nullopt;

    std::string parse_error;
    auto document = toml_support::parse_file(path, &parse_error);
    if (!document)
    {
        if (error)
            *error = parse_error;
        return std::nullopt;
    }

    AppSessionState state;
    state.version = static_cast<int>(toml_support::get_int(*document, "version").value_or(0));
    if (state.version != kSessionStateVersion)
    {
        if (error)
            *error = "Unsupported session state version.";
        return std::nullopt;
    }

    state.session_id = toml_support::get_string(*document, "session_id").value_or("default");
    state.session_name = toml_support::get_string(*document, "session_name").value_or(state.session_id);
    state.active_workspace_id = static_cast<int>(
        toml_support::get_int(*document, "active_workspace_id").value_or(-1));
    state.next_workspace_id = static_cast<int>(
        toml_support::get_int(*document, "next_workspace_id").value_or(0));

    const toml::array* workspaces = (*document)["workspaces"].as_array();
    if (!workspaces)
    {
        if (error)
            *error = "Session state is missing workspaces.";
        return std::nullopt;
    }

    for (const toml::node& node : *workspaces)
    {
        const toml::table* workspace_table = node.as_table();
        if (!workspace_table)
        {
            if (error)
                *error = "Session state workspace entry is not a table.";
            return std::nullopt;
        }

        const auto id = toml_support::get_int(*workspace_table, "id");
        const auto name_user_set = toml_support::get_bool(*workspace_table, "name_user_set");
        const toml::table* host_manager = (*workspace_table)["host_manager"].as_table();
        if (!id || !name_user_set || !host_manager)
        {
            if (error)
                *error = "Session state workspace is missing required fields.";
            return std::nullopt;
        }

        WorkspaceSessionState workspace;
        workspace.id = static_cast<int>(*id);
        workspace.name = toml_support::get_string(*workspace_table, "name").value_or("");
        workspace.name_user_set = *name_user_set;

        auto parsed_host_manager = parse_host_manager_state(*host_manager, error);
        if (!parsed_host_manager)
            return std::nullopt;
        workspace.host_manager = std::move(*parsed_host_manager);
        state.workspaces.push_back(std::move(workspace));
    }

    return state;
}

SessionSummary summarize_session_state(const AppSessionState& state)
{
    SessionSummary summary;
    summary.session_id = state.session_id;
    summary.session_name = state.session_name;
    summary.workspace_count = static_cast<int>(state.workspaces.size());
    summary.has_saved_state = true;
    for (const WorkspaceSessionState& workspace : state.workspaces)
        summary.pane_count += static_cast<int>(workspace.host_manager.panes.size());
    return summary;
}

} // namespace

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

bool save_session_state(const AppSessionState& state, std::string* error)
{
    PERF_MEASURE();
    try
    {
        const std::string normalized_id = state.session_id.empty() ? "default" : state.session_id;
        toml::table document;
        document.insert_or_assign("version", state.version);
        document.insert_or_assign("session_id", normalized_id);
        document.insert_or_assign("session_name", state.session_name.empty() ? normalized_id : state.session_name);
        document.insert_or_assign("active_workspace_id", state.active_workspace_id);
        document.insert_or_assign("next_workspace_id", state.next_workspace_id);

        toml::array workspaces;
        for (const WorkspaceSessionState& workspace : state.workspaces)
        {
            toml::table workspace_table;
            workspace_table.insert_or_assign("id", workspace.id);
            if (!workspace.name.empty())
                workspace_table.insert_or_assign("name", workspace.name);
            workspace_table.insert_or_assign("name_user_set", workspace.name_user_set);
            workspace_table.insert_or_assign(
                "host_manager", serialize_host_manager_state(workspace.host_manager));
            workspaces.push_back(std::move(workspace_table));
        }
        document.insert_or_assign("workspaces", std::move(workspaces));

        const auto path = session_state_path(normalized_id);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::trunc);
        if (!out)
        {
            if (error)
                *error = "Unable to open session state for writing.";
            return false;
        }
        out << document << '\n';
        if (!out)
        {
            if (error)
                *error = "Failed writing session state.";
            return false;
        }
        return true;
    }
    catch (const std::exception& ex)
    {
        if (error)
            *error = ex.what();
        return false;
    }
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

std::optional<AppSessionState> load_session_state(
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

std::optional<AppSessionState> load_session_state(std::string* error)
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
