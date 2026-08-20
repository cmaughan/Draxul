#include "topology_cli.h"

#include <draxul/config_document.h>
#include <draxul/control_plane.h>
#include <draxul/plugin_manager.h>
#include <draxul/remote_terminal_client.h>
#include <draxul/server_client.h>
#include <draxul/topology_client.h>
#include <draxul/topology_layout.h>
#include <draxul/topology_protocol.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>

namespace draxul
{

namespace
{

std::string usage()
{
    return "Usage: draxul <space|tab|pane|split|layout|plugin> <command> [target] [options]\n"
           "Run 'draxul --help' for the complete command list.";
}

bool is_topology_verb(std::string_view noun, std::string_view verb)
{
    if (noun == "space")
        return verb == "list" || verb == "get" || verb == "create"
            || verb == "rename" || verb == "close";
    if (noun == "tab")
        return verb == "list" || verb == "get" || verb == "create"
            || verb == "rename" || verb == "close" || verb == "move";
    if (noun == "pane")
        return verb == "list" || verb == "get" || verb == "split"
            || verb == "rename" || verb == "close" || verb == "swap"
            || verb == "restart" || verb == "read" || verb == "send"
            || verb == "run" || verb == "keys"
            || verb == "wait-output" || verb == "move";
    if (noun == "split")
        return verb == "list" || verb == "set" || verb == "equalize";
    if (noun == "layout")
        return verb == "validate" || verb == "apply";
    if (noun == "plugin")
        return verb == "list" || verb == "get";
    return false;
}

bool requires_target(std::string_view noun, std::string_view verb)
{
    if (verb == "get" || verb == "rename" || verb == "close")
        return true;
    if (noun == "layout")
        return true;
    if (noun == "plugin" && verb == "get")
        return true;
    if (noun == "pane"
        && (verb == "split" || verb == "swap" || verb == "restart"
            || verb == "read" || verb == "send" || verb == "run"
            || verb == "keys" || verb == "wait-output"
            || verb == "move"))
        return true;
    return noun == "tab" && verb == "move"
        || noun == "split" && verb == "set";
}

std::optional<int> parse_int(std::string_view text)
{
    int value = 0;
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{}
        || parsed.ptr != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

std::optional<float> parse_ratio(std::string_view text)
{
    try
    {
        size_t consumed = 0;
        const float value = std::stof(std::string(text), &consumed);
        if (consumed != text.size()
            || value < kTopologyMinSplitRatio
            || value > kTopologyMaxSplitRatio)
            return std::nullopt;
        return value;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<int> parse_duration_ms(std::string_view text)
{
    int multiplier = 1000;
    if (text.ends_with("ms"))
    {
        multiplier = 1;
        text.remove_suffix(2);
    }
    else if (text.ends_with('s'))
        text.remove_suffix(1);
    else if (text.ends_with('m'))
    {
        multiplier = 60 * 1000;
        text.remove_suffix(1);
    }
    const auto value = parse_int(text);
    if (!value || *value <= 0
        || *value > 24 * 60 * 60 * 1000 / multiplier)
        return std::nullopt;
    return *value * multiplier;
}

std::filesystem::path runtime_directory(
    const TopologyCliCommand& command)
{
    if (!command.server_runtime_directory.empty())
        return command.server_runtime_directory;
    if (const char* inherited = std::getenv(
            "DRAXUL_SERVER_RUNTIME_DIR");
        inherited && *inherited)
    {
        return inherited;
    }
    return server_runtime_directory(
        ConfigDocument::default_path().parent_path());
}

struct LocatedTab
{
    const TopologySpace* space = nullptr;
    const TopologyTab* tab = nullptr;
    size_t space_index = 0;
    size_t tab_index = 0;
};

struct LocatedPane
{
    const TopologySpace* space = nullptr;
    const TopologyTab* tab = nullptr;
    const TopologyPane* pane = nullptr;
    const TopologyNode* leaf = nullptr;
    size_t space_index = 0;
    size_t tab_index = 0;
    size_t pane_index = 0;
};

struct LocatedNode
{
    const TopologySpace* space = nullptr;
    const TopologyTab* tab = nullptr;
    const TopologyNode* node = nullptr;
    size_t space_index = 0;
    size_t tab_index = 0;
    size_t node_index = 0;
};

// Index-tracking wrappers over the shared draxul::find_* traversal in
// <draxul/topology_layout.h>. Indices are recovered from the returned
// element pointers, so identity matching stays single-sourced.
const TopologySpace* find_space(
    const TopologySnapshot& snapshot, std::string_view id,
    size_t* index)
{
    const TopologySpace* space = draxul::find_space(snapshot, id);
    if (space && index)
        *index = static_cast<size_t>(space - snapshot.spaces.data());
    return space;
}

std::optional<LocatedTab> find_tab(
    const TopologySnapshot& snapshot, std::string_view id)
{
    for (size_t si = 0; si < snapshot.spaces.size(); ++si)
    {
        const auto& space = snapshot.spaces[si];
        if (const TopologyTab* tab = draxul::find_tab(space, id))
        {
            return LocatedTab{
                &space, tab,
                si, static_cast<size_t>(tab - space.tabs.data())
            };
        }
    }
    return std::nullopt;
}

std::optional<LocatedPane> find_pane(
    const TopologySnapshot& snapshot, std::string_view id)
{
    for (size_t si = 0; si < snapshot.spaces.size(); ++si)
    {
        const auto& space = snapshot.spaces[si];
        for (size_t ti = 0; ti < space.tabs.size(); ++ti)
        {
            const auto& tab = space.tabs[ti];
            const TopologyPane* pane = draxul::find_pane(tab, id);
            if (!pane)
                continue;
            const auto leaf = std::ranges::find_if(
                tab.nodes, [id](const TopologyNode& node) {
                    return node.is_leaf && node.pane_id == id;
                });
            return LocatedPane{
                &space,
                &tab,
                pane,
                leaf == tab.nodes.end() ? nullptr : &*leaf,
                si,
                ti,
                static_cast<size_t>(pane - tab.panes.data()),
            };
        }
    }
    return std::nullopt;
}

std::optional<LocatedNode> find_node(
    const TopologySnapshot& snapshot, std::string_view id)
{
    for (size_t si = 0; si < snapshot.spaces.size(); ++si)
    {
        const auto& space = snapshot.spaces[si];
        for (size_t ti = 0; ti < space.tabs.size(); ++ti)
        {
            const auto& tab = space.tabs[ti];
            if (const TopologyNode* node
                = draxul::find_node(tab, id))
            {
                return LocatedNode{
                    &space, &tab, node, si, ti,
                    static_cast<size_t>(node - tab.nodes.data())
                };
            }
        }
    }
    return std::nullopt;
}

nlohmann::json space_summary(const TopologySpace& space)
{
    size_t panes = 0;
    for (const auto& tab : space.tabs)
        panes += tab.panes.size();
    return {
        { "id", space.space_id },
        { "name", space.name },
        { "root_directory", space.root_directory },
        { "tab_count", space.tabs.size() },
        { "pane_count", panes },
    };
}

nlohmann::json tab_json(
    const TopologySnapshot& snapshot, const LocatedTab& located)
{
    auto encoded = topology_snapshot_to_json(snapshot)
        ["spaces"][located.space_index]["tabs"]
        [located.tab_index];
    encoded["id"] = located.tab->tab_id;
    encoded["space_id"] = located.space->space_id;
    return encoded;
}

nlohmann::json pane_json(
    const TopologySnapshot& snapshot, const LocatedPane& located)
{
    auto encoded = topology_snapshot_to_json(snapshot)
        ["spaces"][located.space_index]["tabs"]
        [located.tab_index]["panes"][located.pane_index];
    encoded["id"] = located.pane->pane_id;
    encoded["space_id"] = located.space->space_id;
    encoded["tab_id"] = located.tab->tab_id;
    encoded["space_name"] = located.space->name;
    encoded["tab_name"] = located.tab->name;
    encoded["node_id"] = located.leaf
        ? nlohmann::json(located.leaf->node_id)
        : nlohmann::json(nullptr);
    return encoded;
}

nlohmann::json node_json(
    const TopologySnapshot& snapshot, const LocatedNode& located)
{
    auto encoded = topology_snapshot_to_json(snapshot)
        ["spaces"][located.space_index]["tabs"]
        [located.tab_index]["nodes"][located.node_index];
    encoded["id"] = located.node->node_id;
    encoded["space_id"] = located.space->space_id;
    encoded["tab_id"] = located.tab->tab_id;
    return encoded;
}

int print_error(std::string_view code, std::string_view message, bool json)
{
    if (json)
    {
        std::fprintf(stderr, "%s\n", nlohmann::json{
                                         { "ok", false },
                                         { "error", { { "code", code }, { "message", message } } },
                                     }
                                         .dump(2)
                                         .c_str());
    }
    else
    {
        std::fprintf(stderr, "%.*s: %.*s\n",
            static_cast<int>(code.size()), code.data(),
            static_cast<int>(message.size()), message.data());
    }
    return 1;
}

void print_result(const nlohmann::json& value, bool json)
{
    if (json)
    {
        std::printf("%s\n", value.dump(2).c_str());
        return;
    }
    if (value.is_array())
    {
        for (const auto& row : value)
        {
            std::printf("%-16s %s\n",
                row.value("id", row.value("node_id", "")).c_str(),
                row.value("name", row.value("kind", "")).c_str());
        }
        return;
    }
    std::printf("%s\n", value.dump(2).c_str());
}

std::string snapshot_text(const TerminalSemanticSnapshot& snapshot)
{
    std::string text;
    for (int row = 0; row < snapshot.rows; ++row)
    {
        std::string line;
        for (int col = 0; col < snapshot.cols; ++col)
        {
            const size_t index
                = static_cast<size_t>(row) * snapshot.cols + col;
            if (index < snapshot.cells.size())
                line += snapshot.cells[index].text;
        }
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
        text += line;
        if (row + 1 < snapshot.rows)
            text.push_back('\n');
    }
    return text;
}

std::vector<std::string> bottom_lines(
    std::string_view text, int maximum)
{
    std::vector<std::string> lines;
    size_t begin = 0;
    while (begin <= text.size())
    {
        const size_t end = text.find('\n', begin);
        lines.emplace_back(text.substr(begin,
            end == std::string_view::npos
                ? text.size() - begin
                : end - begin));
        if (end == std::string_view::npos)
            break;
        begin = end + 1;
    }
    if (lines.size() > static_cast<size_t>(maximum))
        lines.erase(lines.begin(), lines.end() - maximum);
    return lines;
}

std::optional<std::string> encode_key(std::string_view key)
{
    std::string normalized(key);
    std::ranges::transform(normalized, normalized.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
    if (normalized == "enter" || normalized == "return")
        return "\r";
    if (normalized == "tab")
        return "\t";
    if (normalized == "escape" || normalized == "esc")
        return "\x1b";
    if (normalized == "backspace")
        return "\x7f";
    if (normalized == "space")
        return " ";
    if (normalized == "up")
        return "\x1b[A";
    if (normalized == "down")
        return "\x1b[B";
    if (normalized == "right")
        return "\x1b[C";
    if (normalized == "left")
        return "\x1b[D";
    if (normalized == "home")
        return "\x1b[H";
    if (normalized == "end")
        return "\x1b[F";
    if (normalized == "pageup")
        return "\x1b[5~";
    if (normalized == "pagedown")
        return "\x1b[6~";
    if (normalized == "insert")
        return "\x1b[2~";
    if (normalized == "delete")
        return "\x1b[3~";
    if (normalized.size() == 6 && normalized.starts_with("ctrl-")
        && normalized[5] >= 'a' && normalized[5] <= 'z')
        return std::string(1, normalized[5] - 'a' + 1);
    if (key.size() == 1)
        return std::string(key);
    return std::nullopt;
}

} // namespace

ParseTopologyCliResult parse_topology_cli(
    const std::vector<std::string>& args)
{
    ParseTopologyCliResult parsed;
    if (args.size() < 2
        || (args[1] != "space" && args[1] != "tab"
            && args[1] != "pane" && args[1] != "split"
            && args[1] != "layout" && args[1] != "plugin"))
    {
        return parsed;
    }
    if (args.size() < 3)
        return parsed;

    const std::string& noun = args[1];
    const std::string& verb = args[2];
    if (!is_topology_verb(noun, verb))
        return parsed;
    parsed.recognized = true;

    TopologyCliCommand command{
        .noun = noun,
        .verb = verb,
    };
    if (const char* inherited = std::getenv("DRAXUL_SESSION_ID");
        inherited && *inherited)
    {
        command.session_id = inherited;
    }
    size_t position = 3;
    if (requires_target(noun, verb))
    {
        if (noun != "layout" && position < args.size()
            && args[position] == "--current")
        {
            const char* variable = noun == "space"
                ? "DRAXUL_SPACE_ID"
                : noun == "tab"
                ? "DRAXUL_TAB_ID"
                : noun == "split"
                ? nullptr
                : "DRAXUL_PANE_ID";
            const char* inherited = variable
                ? std::getenv(variable)
                : nullptr;
            if (!inherited || !*inherited)
            {
                parsed.error
                    = "--current is unavailable outside a Draxul pane context.";
                return parsed;
            }
            command.target_id = inherited;
            ++position;
        }
        else
        {
            if (position >= args.size()
                || args[position].starts_with("--"))
            {
                parsed.error
                    = "This command requires a target id.\n" + usage();
                return parsed;
            }
            command.target_id = args[position++];
        }
        if (noun == "pane" && verb == "swap")
        {
            if (position >= args.size() || args[position].starts_with("--"))
            {
                parsed.error = "pane swap requires two pane ids.";
                return parsed;
            }
            command.secondary_id = args[position++];
        }
    }

    while (position < args.size())
    {
        const std::string& option = args[position++];
        const auto take_value = [&]() -> std::optional<std::string> {
            if (position >= args.size())
                return std::nullopt;
            return args[position++];
        };
        if (option == "--json")
            command.json = true;
        else if (option == "--dry-run")
            command.dry_run = true;
        else if (option == "--session")
        {
            const auto value = take_value();
            if (!value || value->empty())
            {
                parsed.error = "--session requires an id.";
                return parsed;
            }
            command.session_id = *value;
        }
        else if (option == "--server-runtime-dir")
        {
            const auto value = take_value();
            if (!value || value->empty())
            {
                parsed.error = "--server-runtime-dir requires a path.";
                return parsed;
            }
            command.server_runtime_directory = *value;
        }
        else if (option == "--current")
        {
            parsed.error
                = "Place --current where this command expects its target id.";
            return parsed;
        }
        else if (option == "--space")
        {
            const auto value = take_value();
            if (!value || value->empty())
            {
                parsed.error = "--space requires an id.";
                return parsed;
            }
            command.space_id = *value;
        }
        else if (option == "--tab")
        {
            const auto value = take_value();
            if (!value || value->empty())
            {
                parsed.error = "--tab requires an id.";
                return parsed;
            }
            command.tab_id = *value;
        }
        else if (option == "--target")
        {
            const auto value = take_value();
            if (!value || value->empty())
            {
                parsed.error = "--target requires a pane id.";
                return parsed;
            }
            command.secondary_id = *value;
        }
        else if (option == "--name")
        {
            const auto value = take_value();
            if (!value || value->empty())
            {
                parsed.error = "--name requires a value.";
                return parsed;
            }
            command.name = *value;
        }
        else if (option == "--root")
        {
            const auto value = take_value();
            if (!value)
            {
                parsed.error = "--root requires a path.";
                return parsed;
            }
            command.root_directory = *value;
        }
        else if (option == "--cwd")
        {
            const auto value = take_value();
            if (!value)
            {
                parsed.error = "--cwd requires a path.";
                return parsed;
            }
            command.working_directory = *value;
        }
        else if (option == "--plugin")
        {
            const auto value = take_value();
            if (!value || !PluginManager::valid_plugin_id(*value))
            {
                parsed.error = "--plugin requires a valid plugin id.";
                return parsed;
            }
            command.plugin_id = *value;
        }
        else if (option == "--plugin-config")
        {
            const auto value = take_value();
            try
            {
                const auto config = value ? nlohmann::json::parse(*value)
                                          : nlohmann::json{};
                if (!config.is_object())
                    throw std::runtime_error("not an object");
                command.plugin_config_json = config.dump();
                if (command.plugin_config_json.size() > kTopologyMaxTextBytes)
                    throw std::runtime_error("too large");
            }
            catch (...)
            {
                parsed.error = "--plugin-config requires a bounded JSON object.";
                return parsed;
            }
        }
        else if (option == "--text" || option == "--command")
        {
            const auto value = take_value();
            if (!value || value->empty())
            {
                parsed.error = option + " requires a value.";
                return parsed;
            }
            command.text = *value;
        }
        else if (option == "--timeout")
        {
            const auto value = take_value();
            const auto duration = value
                ? parse_duration_ms(*value)
                : std::nullopt;
            if (!duration)
            {
                parsed.error
                    = "--timeout requires a positive duration such as 30s.";
                return parsed;
            }
            command.timeout_ms = *duration;
        }
        else if (option == "--lines")
        {
            const auto value = take_value();
            const auto lines = value ? parse_int(*value) : std::nullopt;
            if (!lines || *lines < 1 || *lines > 200)
            {
                parsed.error = "--lines must be between 1 and 200.";
                return parsed;
            }
            command.lines = *lines;
        }
        else if (option == "--direction")
        {
            const auto value = take_value();
            if (!value || (*value != "right" && *value != "down" && *value != "left" && *value != "up"))
            {
                parsed.error = "--direction must be left, right, up, or down.";
                return parsed;
            }
            command.direction = *value;
        }
        else if (option == "--ratio")
        {
            const auto value = take_value();
            const auto ratio = value ? parse_ratio(*value) : std::nullopt;
            if (!ratio)
            {
                parsed.error = "--ratio must be between 0.1 and 0.9.";
                return parsed;
            }
            command.ratio = *ratio;
        }
        else if (option == "--delta")
        {
            const auto value = take_value();
            const auto delta = value ? parse_int(*value) : std::nullopt;
            if (!delta || (*delta != -1 && *delta != 1))
            {
                parsed.error = "--delta must be -1 or 1.";
                return parsed;
            }
            command.move_delta = *delta;
        }
        else
        {
            if (noun == "pane" && verb == "keys"
                && !option.starts_with("--"))
                command.values.push_back(option);
            else
            {
                parsed.error = "Unknown topology option: " + option;
                return parsed;
            }
        }
    }

    if ((verb == "rename" || noun == "space" && verb == "create")
        && command.name.empty())
    {
        parsed.error = "This command requires --name.";
        return parsed;
    }
    if (noun == "tab" && (verb == "list" || verb == "create")
        && command.space_id.empty())
    {
        if (const char* inherited = std::getenv("DRAXUL_SPACE_ID");
            inherited && *inherited)
            command.space_id = inherited;
        else
        {
            parsed.error = "This command requires --space <space-id>.";
            return parsed;
        }
    }
    if (noun == "split" && (verb == "list" || verb == "equalize")
        && command.tab_id.empty())
    {
        if (const char* inherited = std::getenv("DRAXUL_TAB_ID");
            inherited && *inherited)
            command.tab_id = inherited;
        else
        {
            parsed.error = "This command requires --tab <tab-id>.";
            return parsed;
        }
    }
    if (noun == "tab" && verb == "move" && command.move_delta == 0)
    {
        parsed.error = "tab move requires --delta -1 or --delta 1.";
        return parsed;
    }
    if (noun == "pane"
        && (verb == "send" || verb == "run"
            || verb == "wait-output")
        && command.text.empty())
    {
        parsed.error = verb == "run"
            ? "pane run requires --command."
            : "pane " + verb + " requires --text.";
        return parsed;
    }
    if (noun == "pane" && verb == "keys"
        && command.values.empty())
    {
        parsed.error = "pane keys requires at least one key name.";
        return parsed;
    }
    if (noun == "pane" && verb == "move"
        && command.secondary_id.empty())
    {
        parsed.error = "pane move requires --target <pane-id>.";
        return parsed;
    }
    if (!command.plugin_config_json.empty() && command.plugin_id.empty())
    {
        parsed.error = "--plugin-config requires --plugin.";
        return parsed;
    }
    if (!command.plugin_id.empty()
        && !((noun == "pane" && verb == "split")
            || (noun == "tab" && verb == "create")))
    {
        parsed.error = "--plugin is supported by pane split and tab create.";
        return parsed;
    }
    if (!command.plugin_id.empty()
        && !command.working_directory.empty())
    {
        parsed.error = "--cwd and --plugin are mutually exclusive.";
        return parsed;
    }
    if (noun == "layout" && verb == "validate")
        command.dry_run = true;
    parsed.command = std::move(command);
    return parsed;
}

int run_topology_cli(const TopologyCliCommand& command)
{
    if (command.noun == "plugin")
    {
        const auto manager = PluginManager::discover_default();
        nlohmann::json output = nlohmann::json::array();
        for (const auto& manifest : manager->manifests())
        {
            if (command.verb == "get" && manifest.id != command.target_id)
                continue;
            nlohmann::json entry = {
                { "id", manifest.id },
                { "name", manifest.name },
                { "version", manifest.version },
                { "abi_version", manifest.abi_version },
                { "library", manifest.library_path.string() },
                { "user_installed", manifest.user_installed },
                { "available", manifest.error.empty() },
                { "error", manifest.error },
            };
            if (command.verb == "get" && manifest.error.empty())
            {
                std::string load_error;
                const auto loaded = manager->load(
                    manifest.id, load_error);
                entry["available"] = static_cast<bool>(loaded);
                entry["error"] = load_error;
                if (loaded)
                {
                    entry["supported_backends"]
                        = loaded->api().supported_backends;
                }
            }
            output.push_back(std::move(entry));
        }
        if (command.verb == "get")
        {
            if (output.empty())
                return print_error("plugin_not_found", "Plugin was not found.", command.json);
            print_result(output.front(), command.json);
        }
        else
            print_result(output, command.json);
        return 0;
    }

    const auto runtime = runtime_directory(command);
    const std::string probe_client_id = make_server_client_id();
    const auto probe = ServerClient::probe({
        .runtime_directory = runtime,
        .client_id = probe_client_id,
        .launch_if_missing = false,
    });
    if (!probe.ready())
    {
        return print_error(
            probe.error_code.empty()
                ? "server_unavailable"
                : probe.error_code,
            probe.error_message.empty()
                ? "The Draxul server is unavailable."
                : probe.error_message,
            command.json);
    }
    if (std::ranges::find(probe.welcome->capabilities,
            "topology-control-v2")
        == probe.welcome->capabilities.end())
    {
        return print_error("unsupported_server",
            "The running Draxul server predates headless topology control; stop it and retry with this build.",
            command.json);
    }
    const bool plugin_mutation = !command.plugin_id.empty();
    if (plugin_mutation
        && std::ranges::find(probe.welcome->capabilities,
               "client-plugin-pane-v1")
            == probe.welcome->capabilities.end())
    {
        return print_error("unsupported_server",
            "The running Draxul server does not support plugin panes.",
            command.json);
    }
    std::string disconnect_error;
    ServerClient::disconnect(runtime, probe_client_id,
        disconnect_error, probe.welcome->connection_token);
    const std::string client_id = make_server_client_id();
    if (command.noun == "layout")
    {
        std::string source;
        if (command.target_id == "-")
        {
            source.assign(std::istreambuf_iterator<char>(std::cin),
                std::istreambuf_iterator<char>());
        }
        else
        {
            std::ifstream input(command.target_id, std::ios::binary);
            if (!input)
                return print_error("layout_file_unavailable",
                    "Could not open layout file: " + command.target_id,
                    command.json);
            source.assign(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
        }
        nlohmann::json layout;
        try
        {
            layout = nlohmann::json::parse(source);
        }
        catch (const std::exception& exception)
        {
            return print_error("invalid_json", exception.what(), command.json);
        }
        auto result = ControlClient::request(
            namespaced_control_id(kServerControlId, runtime),
            runtime, "topology.layout_apply",
            {
                { "session_id", command.session_id },
                { "layout", std::move(layout) },
                { "dry_run", command.dry_run },
            });
        if (!result.ok)
            return print_error(result.error_code,
                result.error_message, command.json);
        print_result(result.result, command.json);
        return 0;
    }

    TopologyClient client({
        .runtime_directory = runtime,
        .client_id = client_id,
        .session_id = command.session_id,
    });
    std::string error;
    if (!client.refresh(error))
    {
        return print_error(
            client.last_error_code().empty()
                ? "server_unavailable"
                : client.last_error_code(),
            error, command.json);
    }

    const TopologySnapshot& snapshot = client.snapshot();
    const bool terminal_command
        = command.noun == "pane"
        && (command.verb == "read" || command.verb == "send"
            || command.verb == "run" || command.verb == "keys"
            || command.verb == "wait-output");
    if (terminal_command)
    {
        const auto located = find_pane(snapshot, command.target_id);
        if (!located)
            return print_error("pane_not_found", "Pane was not found.", command.json);
        if (located->pane->domain != TopologyPaneDomain::ServerTerminal
            || located->pane->terminal_id.empty())
        {
            return print_error("client_local_pane",
                "Only server terminal panes support headless terminal control.",
                command.json);
        }

        const std::string terminal_client_id = make_server_client_id();
        RemoteTerminalClient terminal({
            .runtime_directory = runtime_directory(command),
            .client_id = terminal_client_id,
            .session_id = command.session_id,
            .method_prefix = "terminal",
            .terminal_id = located->pane->terminal_id,
        });
        if (!terminal.attach(error))
        {
            return print_error(
                terminal.last_error_code().empty()
                    ? "terminal_attach_failed"
                    : terminal.last_error_code(),
                error, command.json);
        }
        const auto disconnect = [&]() {
            std::string ignored;
            terminal.disconnect(ignored,
                static_cast<uint64_t>(
                    std::chrono::steady_clock::now()
                        .time_since_epoch()
                        .count()));
        };

        if (command.verb == "send" || command.verb == "run"
            || command.verb == "keys")
        {
            if (!terminal.projection().is_controller(terminal_client_id)
                && !terminal.take_control(error, 1))
            {
                disconnect();
                return print_error(
                    terminal.last_error_code().empty()
                        ? "take_control_failed"
                        : terminal.last_error_code(),
                    error, command.json);
            }
            std::string input;
            if (command.verb == "keys")
            {
                for (const auto& key : command.values)
                {
                    const auto encoded = encode_key(key);
                    if (!encoded)
                    {
                        disconnect();
                        return print_error("unknown_key",
                            "Unknown key name: " + key, command.json);
                    }
                    input += *encoded;
                }
            }
            else
            {
                input = command.text;
                if (command.verb == "run")
                    input.push_back('\r');
            }
            if (!terminal.send_input(input, error, 2))
            {
                disconnect();
                return print_error(
                    terminal.last_error_code().empty()
                        ? "terminal_input_failed"
                        : terminal.last_error_code(),
                    error, command.json);
            }
            disconnect();
            print_result({
                             { "ok", true },
                             { "command", "pane." + command.verb },
                             { "pane_id", command.target_id },
                             { "terminal_id", located->pane->terminal_id },
                             { "bytes_sent", input.size() },
                         },
                command.json);
            return 0;
        }

        if (command.verb == "wait-output")
        {
            const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(command.timeout_ms);
            while (snapshot_text(terminal.projection().snapshot())
                        .find(command.text)
                    == std::string::npos
                && std::chrono::steady_clock::now() < deadline)
            {
                bool changed = false;
                if (!terminal.poll(changed, error))
                {
                    disconnect();
                    return print_error(
                        terminal.last_error_code().empty()
                            ? "terminal_poll_failed"
                            : terminal.last_error_code(),
                        error, command.json);
                }
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(50));
            }
            const std::string text
                = snapshot_text(terminal.projection().snapshot());
            if (text.find(command.text) == std::string::npos)
            {
                disconnect();
                return print_error("timeout",
                    "Timed out waiting for pane output.", command.json);
            }
        }

        const auto& projection = terminal.projection();
        const std::string text = snapshot_text(projection.snapshot());
        const auto lines = bottom_lines(text, command.lines);
        nlohmann::json output{
            { "ok", true },
            { "command", "pane." + command.verb },
            { "pane_id", command.target_id },
            { "terminal_id", located->pane->terminal_id },
            { "text", text },
            { "lines", lines },
            { "process_running", projection.pane().process_running },
            { "exit_code", projection.pane().exit_code ? nlohmann::json(*projection.pane().exit_code) : nlohmann::json(nullptr) },
        };
        disconnect();
        if (command.json)
            print_result(output, true);
        else
        {
            for (const auto& line : lines)
                std::printf("%s\n", line.c_str());
        }
        return 0;
    }

    if (command.verb == "list" || command.verb == "get")
    {
        nlohmann::json output = command.verb == "list"
            ? nlohmann::json::array()
            : nlohmann::json{};
        if (command.noun == "space")
        {
            if (command.verb == "list")
            {
                for (const auto& space : snapshot.spaces)
                    output.push_back(space_summary(space));
            }
            else
            {
                size_t index = 0;
                if (!find_space(snapshot, command.target_id, &index))
                    return print_error("space_not_found", "Space was not found.", command.json);
                output = topology_snapshot_to_json(snapshot)["spaces"][index];
                output["id"] = command.target_id;
            }
        }
        else if (command.noun == "tab")
        {
            if (command.verb == "list")
            {
                const auto* space = find_space(snapshot, command.space_id);
                if (!space)
                    return print_error("space_not_found", "Space was not found.", command.json);
                for (const auto& tab : space->tabs)
                {
                    const auto located = find_tab(snapshot, tab.tab_id);
                    output.push_back(tab_json(snapshot, *located));
                }
            }
            else
            {
                const auto located = find_tab(snapshot, command.target_id);
                if (!located)
                    return print_error("tab_not_found", "Tab was not found.", command.json);
                output = tab_json(snapshot, *located);
            }
        }
        else if (command.noun == "pane")
        {
            if (command.verb == "list")
            {
                for (const auto& space : snapshot.spaces)
                {
                    if (!command.space_id.empty()
                        && space.space_id != command.space_id)
                        continue;
                    for (const auto& tab : space.tabs)
                    {
                        if (!command.tab_id.empty()
                            && tab.tab_id != command.tab_id)
                            continue;
                        for (const auto& pane : tab.panes)
                        {
                            const auto located = find_pane(snapshot, pane.pane_id);
                            output.push_back(pane_json(snapshot, *located));
                        }
                    }
                }
            }
            else
            {
                const auto located = find_pane(snapshot, command.target_id);
                if (!located)
                    return print_error("pane_not_found",
                        "Pane was not found: " + command.target_id,
                        command.json);
                output = pane_json(snapshot, *located);
            }
        }
        else
        {
            const auto located_tab = find_tab(snapshot, command.tab_id);
            if (!located_tab)
                return print_error("tab_not_found", "Tab was not found.", command.json);
            for (const auto& node : located_tab->tab->nodes)
            {
                if (node.is_leaf)
                    continue;
                const auto located = find_node(snapshot, node.node_id);
                output.push_back(node_json(snapshot, *located));
            }
        }
        print_result(output, command.json);
        return 0;
    }

    TopologyCommand mutation{
        .client_id = client_id,
        .command_id = make_server_client_id(),
    };
    if (command.noun == "space")
    {
        mutation.kind = command.verb == "create"
            ? TopologyCommandKind::CreateSpace
            : command.verb == "rename"
            ? TopologyCommandKind::RenameSpace
            : TopologyCommandKind::CloseSpace;
        mutation.space_id = command.target_id;
        mutation.name = command.name;
        mutation.root_directory = command.root_directory;
        mutation.server_working_directory
            = command.working_directory;
        mutation.pane_domain = TopologyPaneDomain::ServerTerminal;
    }
    else if (command.noun == "tab")
    {
        if (command.verb == "create")
        {
            mutation.kind = TopologyCommandKind::CreateTab;
            mutation.space_id = command.space_id;
            mutation.name = command.name.empty() ? "Tab" : command.name;
            mutation.server_working_directory
                = command.working_directory;
            mutation.pane_domain = TopologyPaneDomain::ServerTerminal;
            if (!command.plugin_id.empty())
            {
                mutation.pane_domain = TopologyPaneDomain::ClientLocal;
                mutation.client_host_kind = "plugin";
                mutation.client_plugin_id = command.plugin_id;
                mutation.client_plugin_config_json
                    = command.plugin_config_json.empty()
                    ? "{}"
                    : command.plugin_config_json;
            }
        }
        else
        {
            const auto located = find_tab(snapshot, command.target_id);
            if (!located)
                return print_error("tab_not_found", "Tab was not found.", command.json);
            mutation.kind = command.verb == "rename"
                ? TopologyCommandKind::RenameTab
                : command.verb == "close"
                ? TopologyCommandKind::CloseTab
                : TopologyCommandKind::MoveTab;
            mutation.space_id = located->space->space_id;
            mutation.tab_id = located->tab->tab_id;
            mutation.name = command.name;
            mutation.move_delta = command.move_delta;
        }
    }
    else if (command.noun == "pane")
    {
        const auto located = find_pane(snapshot, command.target_id);
        if (!located)
            return print_error("pane_not_found", "Pane was not found.", command.json);
        mutation.space_id = located->space->space_id;
        mutation.tab_id = located->tab->tab_id;
        mutation.pane_id = located->pane->pane_id;
        mutation.name = command.name;
        if (command.verb == "split")
        {
            mutation.kind = TopologyCommandKind::SplitPane;
            mutation.direction
                = command.direction == "down"
                    || command.direction == "up"
                ? TopologySplitDirection::Horizontal
                : TopologySplitDirection::Vertical;
            mutation.ratio = command.ratio;
            mutation.place_before
                = command.direction == "left"
                || command.direction == "up";
            mutation.server_working_directory
                = command.working_directory;
            mutation.pane_domain = TopologyPaneDomain::ServerTerminal;
            if (!command.plugin_id.empty())
            {
                mutation.pane_domain = TopologyPaneDomain::ClientLocal;
                mutation.client_host_kind = "plugin";
                mutation.client_plugin_id = command.plugin_id;
                mutation.client_plugin_config_json
                    = command.plugin_config_json.empty()
                    ? "{}"
                    : command.plugin_config_json;
            }
        }
        else if (command.verb == "rename")
            mutation.kind = TopologyCommandKind::RenamePane;
        else if (command.verb == "close")
            mutation.kind = TopologyCommandKind::ClosePane;
        else if (command.verb == "restart")
            mutation.kind = TopologyCommandKind::RestartPane;
        else if (command.verb == "move")
        {
            const auto target = find_pane(snapshot, command.secondary_id);
            if (!target || target->tab->tab_id != located->tab->tab_id)
            {
                return print_error("pane_not_found",
                    "Pane move requires two panes in the same tab.", command.json);
            }
            mutation.kind = TopologyCommandKind::MovePane;
            mutation.target_pane_id = target->pane->pane_id;
            mutation.direction
                = command.direction == "down"
                    || command.direction == "up"
                ? TopologySplitDirection::Horizontal
                : TopologySplitDirection::Vertical;
            mutation.place_before
                = command.direction == "left"
                || command.direction == "up";
            mutation.ratio = command.ratio;
        }
        else
        {
            const auto target = find_pane(snapshot, command.secondary_id);
            if (!target || target->tab->tab_id != located->tab->tab_id)
            {
                return print_error("pane_not_found",
                    "Pane swap requires two panes in the same tab.", command.json);
            }
            mutation.kind = TopologyCommandKind::SwapPane;
            mutation.target_pane_id = target->pane->pane_id;
        }
    }
    else
    {
        const auto tab = command.verb == "equalize"
            ? find_tab(snapshot, command.tab_id)
            : std::optional<LocatedTab>{};
        const auto node = command.verb == "set"
            ? find_node(snapshot, command.target_id)
            : std::optional<LocatedNode>{};
        if (command.verb == "equalize")
        {
            if (!tab)
                return print_error("tab_not_found", "Tab was not found.", command.json);
            mutation.kind = TopologyCommandKind::EqualizeSplits;
            mutation.space_id = tab->space->space_id;
            mutation.tab_id = tab->tab->tab_id;
        }
        else
        {
            if (!node || node->node->is_leaf)
                return print_error("split_not_found", "Split was not found.", command.json);
            mutation.kind = TopologyCommandKind::SetSplitRatio;
            mutation.space_id = node->space->space_id;
            mutation.tab_id = node->tab->tab_id;
            mutation.node_id = node->node->node_id;
            mutation.ratio = command.ratio;
        }
    }

    TopologyCommandResult result;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        mutation.expected_revision = client.snapshot().revision;
        if (client.execute(mutation, result, error))
        {
            nlohmann::json output = topology_command_result_to_json(result);
            output["ok"] = true;
            output["command"] = command.noun + "." + command.verb;
            print_result(output, command.json);
            return 0;
        }
        if (client.last_error_code() != "revision_conflict"
            || !client.refresh(error))
            break;
    }
    return print_error(
        client.last_error_code().empty()
            ? "topology_command_failed"
            : client.last_error_code(),
        error, command.json);
}

} // namespace draxul
