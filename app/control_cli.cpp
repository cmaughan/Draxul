#include "control_cli.h"

#include <draxul/config_document.h>
#include <draxul/control_plane.h>
#include <draxul/server_client.h>
#include <draxul/topology_client.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <future>
#include <nlohmann/json.hpp>
#include <thread>

namespace draxul
{

namespace
{

std::optional<int> parse_int(std::string_view text)
{
    int result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return std::nullopt;
    return result;
}

std::optional<uint64_t> parse_uint64(std::string_view text)
{
    uint64_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return std::nullopt;
    return result;
}

std::string usage()
{
    return "Usage: draxul <space|agent|pane|plugin|ui> <command> [value] "
           "[--session <id>] [--json] [--lines <1-200>]";
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
    if (!value || *value <= 0 || *value > 24 * 60 * 60 * 1000 / multiplier)
        return std::nullopt;
    return *value * multiplier;
}

void print_human(const ControlCliCommand& command, const nlohmann::json& result)
{
    if (command.method == "space.list")
    {
        for (const auto& space : result)
        {
            std::printf("%c %d  %s  (%d tabs, %d panes)\n",
                space.value("active", false) ? '*' : ' ',
                space.value("id", -1),
                space.value("name", "").c_str(),
                space.value("tab_count", 0),
                space.value("pane_count", 0));
        }
        return;
    }
    if (command.method == "agent.list")
    {
        for (const auto& agent : result)
        {
            const auto& route = agent["route"];
            const auto route_text
                = [&route](const char* name) {
                      const auto value = route.find(name);
                      if (value == route.end())
                          return std::string("?");
                      if (value->is_string())
                          return value->get<std::string>();
                      if (value->is_number_integer())
                          return std::to_string(
                              value->get<int64_t>());
                      return std::string("?");
                  };
            const std::string space
                = route_text("space_id");
            const std::string tab
                = route_text("tab_id");
            std::printf("%c %-20s %-10s %-8s space=%s tab=%s pane=%s\n",
                agent.value("focused", false) ? '*' : ' ',
                agent.value("instance_id", "").c_str(),
                agent.value("kind", "").c_str(),
                agent.value("status", "").c_str(),
                space.c_str(),
                tab.c_str(),
                route.value("pane_id", "").c_str());
        }
        return;
    }
    if (command.method == "pane.read")
    {
        for (const auto& line : result.value("lines", nlohmann::json::array()))
            std::printf("%s\n", line.get<std::string>().c_str());
        return;
    }
    if (command.method == "ui.list")
    {
        for (const auto& ui : result)
        {
            std::printf("%s  %s\n",
                ui.value("control_id", "").c_str(),
                ui.value("control_runtime_directory", "").c_str());
        }
        return;
    }
    std::printf("%s\n", result.dump(2).c_str());
}

} // namespace

ParseControlCliResult parse_control_cli(const std::vector<std::string>& args)
{
    ParseControlCliResult parsed;
    if (args.size() < 2
        || (args[1] != "space" && args[1] != "agent"
            && args[1] != "pane" && args[1] != "plugin"
            && args[1] != "ui"))
        return parsed;
    parsed.recognized = true;
    if (args.size() < 3)
    {
        parsed.error = usage();
        return parsed;
    }

    ControlCliCommand command;
    const std::string noun = args[1];
    const std::string verb = args[2];
    size_t position = 3;
    bool session_explicit = false;

    if (noun == "space" && verb == "list")
        command.method = "space.list";
    else if (noun == "space" && verb == "get")
        command.method = "space.get";
    else if (noun == "space" && verb == "focus")
        command.method = "space.focus";
    else if (noun == "agent" && verb == "list")
        command.method = "agent.list";
    else if (noun == "agent" && verb == "get")
        command.method = "agent.get";
    else if (noun == "agent" && verb == "explain")
        command.method = "agent.explain";
    else if (noun == "agent" && verb == "start")
        command.method = "agent.start";
    else if (noun == "agent" && verb == "focus")
        command.method = "agent.focus";
    else if (noun == "agent" && verb == "restart")
        command.method = "agent.restart";
    else if (noun == "agent"
        && (verb == "send" || verb == "prompt"))
        command.method = "agent.send_text";
    else if (noun == "agent" && verb == "keys")
        command.method = "agent.send_keys";
    else if (noun == "agent" && verb == "wait")
        command.method = "agent.wait";
    else if (noun == "pane" && verb == "read")
        command.method = "pane.read";
    else if (noun == "pane" && verb == "focus")
        command.method = "pane.focus";
    else if (noun == "pane" && verb == "action")
        command.method = "pane.action";
    else if (noun == "pane" && verb == "report-agent-session")
        command.method = "pane.report_agent_session";
    else if (noun == "plugin" && verb == "reload")
        command.method = "plugin.reload";
    else if (noun == "ui" && verb == "list")
        command.method = "ui.list";
    else
    {
        parsed.error = usage();
        return parsed;
    }

    const bool needs_value = command.method == "space.get" || command.method == "space.focus"
        || command.method == "agent.get" || command.method == "agent.start"
        || command.method == "agent.focus" || command.method == "agent.restart"
        || command.method == "agent.send_text"
        || command.method == "agent.send_keys" || command.method == "agent.wait"
        || command.method == "agent.explain" || command.method == "pane.read"
        || command.method == "pane.focus" || command.method == "pane.action"
        || command.method == "pane.report_agent_session"
        || command.method == "plugin.reload";
    if (needs_value)
    {
        if (position >= args.size() || args[position].starts_with("--"))
        {
            parsed.error = "This command requires an id.\n" + usage();
            return parsed;
        }
        command.value = args[position++];
    }

    while (position < args.size())
    {
        if (args[position] == "--json")
        {
            command.json = true;
            ++position;
        }
        else if (args[position] == "--session")
        {
            if (++position >= args.size() || args[position].empty())
            {
                parsed.error = "--session requires an id.";
                return parsed;
            }
            command.session_id = args[position++];
            session_explicit = true;
        }
        else if (args[position] == "--server-runtime-dir")
        {
            if (++position >= args.size()
                || args[position].empty())
            {
                parsed.error
                    = "--server-runtime-dir requires a path.";
                return parsed;
            }
            command.server_runtime_directory
                = args[position++];
        }
        else if (args[position] == "--server-epoch")
        {
            if (++position >= args.size()
                || args[position].empty())
            {
                parsed.error = "--server-epoch requires an id.";
                return parsed;
            }
            command.server_epoch = args[position++];
        }
        else if (args[position] == "--ui")
        {
            if (++position >= args.size() || args[position].empty())
            {
                parsed.error = "--ui requires a control id.";
                return parsed;
            }
            command.control_id = args[position++];
            command.control_id_explicit = true;
        }
        else if (args[position] == "--runtime-generation")
        {
            if (++position >= args.size())
            {
                parsed.error
                    = "--runtime-generation requires an integer.";
                return parsed;
            }
            const auto generation
                = parse_uint64(args[position++]);
            if (!generation || *generation == 0)
            {
                parsed.error
                    = "--runtime-generation is invalid.";
                return parsed;
            }
            command.runtime_generation = *generation;
        }
        else if (args[position] == "--lines")
        {
            if (++position >= args.size())
            {
                parsed.error = "--lines requires a number.";
                return parsed;
            }
            const auto lines = parse_int(args[position++]);
            if (!lines || *lines < 1 || *lines > 200)
            {
                parsed.error = "--lines must be between 1 and 200.";
                return parsed;
            }
            command.lines = *lines;
        }
        else if (args[position] == "--cwd")
        {
            if (++position >= args.size())
            {
                parsed.error = "--cwd requires a path.";
                return parsed;
            }
            command.working_directory = args[position++];
        }
        else if (args[position] == "--space")
        {
            if (++position >= args.size())
            {
                parsed.error = "--space requires an id.";
                return parsed;
            }
            command.route_space_id = args[position++];
            if (command.route_space_id.empty())
            {
                parsed.error = "--space requires an id.";
                return parsed;
            }
            command.space_id = parse_int(command.route_space_id);
        }
        else if (args[position] == "--tab")
        {
            if (++position >= args.size() || args[position].empty())
            {
                parsed.error = "--tab requires an id.";
                return parsed;
            }
            command.route_tab_id = args[position++];
        }
        else if (args[position] == "--pane")
        {
            if (++position >= args.size() || args[position].empty())
            {
                parsed.error = "--pane requires an id.";
                return parsed;
            }
            command.route_pane_id = args[position++];
        }
        else if (args[position] == "--replace")
        {
            command.replace_pane = true;
            ++position;
        }
        else if (args[position] == "--text")
        {
            if (++position >= args.size())
            {
                parsed.error = "--text requires a value.";
                return parsed;
            }
            command.text = args[position++];
        }
        else if (args[position] == "--action")
        {
            if (++position >= args.size() || args[position].empty())
            {
                parsed.error = "--action requires a value.";
                return parsed;
            }
            command.action = args[position++];
        }
        else if (args[position] == "--until")
        {
            if (++position >= args.size())
            {
                parsed.error = "--until requires comma-separated states.";
                return parsed;
            }
            std::string states = args[position++];
            size_t start = 0;
            while (start <= states.size())
            {
                const size_t comma = states.find(',', start);
                command.values.push_back(states.substr(
                    start, comma == std::string::npos ? std::string::npos : comma - start));
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }
        }
        else if (args[position] == "--timeout")
        {
            if (++position >= args.size())
            {
                parsed.error = "--timeout requires a duration.";
                return parsed;
            }
            const auto timeout = parse_duration_ms(args[position++]);
            if (!timeout)
            {
                parsed.error = "--timeout accepts a positive duration such as 30s or 10m.";
                return parsed;
            }
            command.timeout_ms = *timeout;
        }
        else if (args[position] == "--agent-instance")
        {
            if (++position >= args.size())
            {
                parsed.error = "--agent-instance requires an id.";
                return parsed;
            }
            command.agent_instance_id = args[position++];
        }
        else if (args[position] == "--source")
        {
            if (++position >= args.size())
            {
                parsed.error = "--source requires a value.";
                return parsed;
            }
            command.source = args[position++];
        }
        else if (args[position] == "--agent")
        {
            if (++position >= args.size())
            {
                parsed.error = "--agent requires a kind.";
                return parsed;
            }
            command.agent_kind = args[position++];
        }
        else if (args[position] == "--integration-version")
        {
            if (++position >= args.size())
            {
                parsed.error = "--integration-version requires an integer.";
                return parsed;
            }
            const auto version = parse_uint64(args[position++]);
            if (!version || *version == 0 || *version > UINT32_MAX)
            {
                parsed.error = "--integration-version is invalid.";
                return parsed;
            }
            command.integration_version = static_cast<uint32_t>(*version);
        }
        else if (args[position] == "--sequence")
        {
            if (++position >= args.size())
            {
                parsed.error = "--sequence requires an integer.";
                return parsed;
            }
            const auto sequence = parse_uint64(args[position++]);
            if (!sequence)
            {
                parsed.error = "--sequence is invalid.";
                return parsed;
            }
            command.sequence = *sequence;
        }
        else if (args[position] == "--session-ref")
        {
            if (++position >= args.size())
            {
                parsed.error = "--session-ref requires a value.";
                return parsed;
            }
            command.reference_value = args[position++];
        }
        else if (args[position] == "--ref-kind")
        {
            if (++position >= args.size())
            {
                parsed.error = "--ref-kind requires id or path.";
                return parsed;
            }
            command.reference_kind = args[position++];
        }
        else if (args[position] == "--" && command.method == "agent.start")
        {
            ++position;
            command.arguments.insert(
                command.arguments.end(), args.begin() + position, args.end());
            position = args.size();
        }
        else if (command.method == "agent.send_keys"
            && !args[position].starts_with("--"))
        {
            command.values.push_back(args[position++]);
        }
        else
        {
            parsed.error = "Unknown control option: " + args[position];
            return parsed;
        }
    }

    if (command.method != "pane.read" && command.lines != 50)
    {
        parsed.error = "--lines is only valid for pane read.";
        return parsed;
    }
    if ((command.method == "space.get" || command.method == "space.focus")
        && !parse_int(command.value))
    {
        parsed.error = "Space id must be an integer.";
        return parsed;
    }
    if (command.method == "agent.send_text" && command.text.empty())
    {
        parsed.error = "agent send requires --text.";
        return parsed;
    }
    if (command.method == "pane.action" && command.action.empty())
    {
        parsed.error = "pane action requires --action.";
        return parsed;
    }
    if (command.method != "pane.action" && !command.action.empty())
    {
        parsed.error = "--action is only valid for pane action.";
        return parsed;
    }
    if (command.method == "agent.send_keys" && command.values.empty())
    {
        parsed.error = "agent keys requires at least one key.";
        return parsed;
    }
    if (command.method == "pane.report_agent_session"
        && (command.agent_instance_id.empty() || command.source.empty()
            || command.agent_kind.empty() || command.integration_version == 0
            || command.sequence == 0 || command.reference_value.empty()
            || (command.reference_kind != "id"
                && command.reference_kind != "path")))
    {
        parsed.error = "pane report-agent-session requires --agent-instance, --source, "
                       "--agent, --integration-version, --sequence, and --session-ref.";
        return parsed;
    }
    if (command.replace_pane && command.method != "agent.start")
    {
        parsed.error = "--replace is only valid for agent start.";
        return parsed;
    }
    if (command.control_id_explicit
        && command.method != "pane.focus"
        && command.method != "pane.action")
    {
        parsed.error = "--ui is only valid for pane focus and pane action.";
        return parsed;
    }
    if (command.method == "agent.start")
    {
        if (command.route_space_id.empty())
        {
            if (const char* value = std::getenv("DRAXUL_SPACE_ID");
                value && *value)
                command.route_space_id = value;
        }
        if (command.route_tab_id.empty())
        {
            if (const char* value = std::getenv("DRAXUL_TAB_ID");
                value && *value)
                command.route_tab_id = value;
        }
        if (command.route_pane_id.empty())
        {
            if (const char* value = std::getenv("DRAXUL_PANE_ID");
                value && *value)
                command.route_pane_id = value;
        }
        if (command.replace_pane
            && command.route_pane_id.empty())
        {
            parsed.error
                = "agent start --replace requires --pane <pane-id> or an enclosing Draxul pane context.";
            return parsed;
        }
    }
    if (command.server_runtime_directory.empty())
    {
        if (const char* value
            = std::getenv("DRAXUL_SERVER_RUNTIME_DIR");
            value && *value)
            command.server_runtime_directory = value;
    }
    if (!session_explicit)
    {
        if (const char* value = std::getenv("DRAXUL_SESSION_ID");
            value && *value)
            command.session_id = value;
    }
    if (!command.control_id_explicit)
    {
        if (const char* value = std::getenv("DRAXUL_CONTROL_ID");
            value && *value)
        {
            command.control_id = value;
            command.control_id_explicit = true;
        }
    }
    if (command.control_id.empty())
        command.control_id = command.session_id;
    parsed.command = std::move(command);
    return parsed;
}

int run_control_cli(const ControlCliCommand& command)
{
    nlohmann::json params = nlohmann::json::object();
    if (command.method == "space.get" || command.method == "space.focus")
        params["id"] = *parse_int(command.value);
    else if (command.method == "agent.get" || command.method == "agent.explain")
        params["instance_id"] = command.value;
    else if (command.method == "pane.read")
    {
        params["pane_id"] = command.value;
        params["lines"] = command.lines;
    }
    else if (command.method == "pane.focus")
        params["pane_id"] = command.value;
    else if (command.method == "pane.action")
    {
        params["pane_id"] = command.value;
        params["action"] = command.action;
    }
    else if (command.method == "plugin.reload")
        params["plugin_id"] = command.value;
    else if (command.method == "agent.start")
    {
        params["profile_id"] = command.value;
        params["args"] = command.arguments;
        if (!command.working_directory.empty())
            params["cwd"] = command.working_directory;
        if (command.space_id
            && !command.route_space_id.starts_with("space-"))
            params["space_id"] = *command.space_id;
        else if (!command.route_space_id.empty())
            params["space_id"] = command.route_space_id;
        if (!command.route_tab_id.empty())
            params["tab_id"] = command.route_tab_id;
        if (!command.route_pane_id.empty())
            params["pane_id"] = command.route_pane_id;
        if (command.replace_pane)
            params["replace_pane"] = true;
    }
    else if (command.method == "agent.focus"
        || command.method == "agent.restart")
        params["instance_id"] = command.value;
    else if (command.method == "agent.send_text")
    {
        params["instance_id"] = command.value;
        params["text"] = command.text;
    }
    else if (command.method == "agent.send_keys")
    {
        params["instance_id"] = command.value;
        params["keys"] = command.values;
    }
    else if (command.method == "agent.wait")
    {
        params["instance_id"] = command.value;
        params["until"] = command.values;
    }
    else if (command.method == "pane.report_agent_session")
    {
        params["pane_id"] = command.value;
        params["agent_instance_id"] = command.agent_instance_id;
        params["source"] = command.source;
        params["agent"] = command.agent_kind;
        params["integration_version"] = command.integration_version;
        params["sequence"] = command.sequence;
        params["ref_kind"] = command.reference_kind;
        params["ref_value"] = command.reference_value;
        if (!command.server_epoch.empty())
            params["server_epoch"] = command.server_epoch;
        if (command.runtime_generation != 0)
        {
            params["runtime_generation"]
                = command.runtime_generation;
        }
    }
    const bool mutating_agent_request
        = command.method == "agent.start"
        || command.method == "agent.restart"
        || command.method == "agent.send_text"
        || command.method == "agent.send_keys";
    if (mutating_agent_request)
    {
        // This key deduplicates retries made against one endpoint. The App
        // endpoint and the headless server have separate idempotency domains,
        // so an ambiguous mutation must never fall through from one to the
        // other.
        params["request_id"] = make_server_client_id();
    }

    const auto runtime = control_runtime_directory(ConfigDocument::default_path().parent_path());
    const auto server_runtime
        = command.server_runtime_directory.empty()
        ? server_runtime_directory(
              ConfigDocument::default_path().parent_path())
        : std::filesystem::path(
              command.server_runtime_directory);
    const bool supports_headless_server
        = command.method == "ui.list"
        || command.method == "agent.list"
        || command.method == "agent.get"
        || command.method == "agent.explain"
        || command.method == "agent.start"
        || command.method == "agent.restart"
        || command.method == "agent.send_text"
        || command.method == "agent.send_keys"
        || command.method == "agent.wait"
        || command.method == "pane.read"
        || command.method
            == "pane.report_agent_session";
    if (command.replace_pane)
    {
        if (!params.contains("space_id")
            || !params.contains("tab_id"))
        {
            TopologyClient topology({
                .runtime_directory = server_runtime,
                .client_id = make_server_client_id(),
                .session_id = command.session_id,
            });
            std::string topology_error;
            if (!topology.refresh(topology_error))
            {
                std::fprintf(stderr,
                    "server_unavailable: %s\n",
                    topology_error.c_str());
                return 1;
            }
            bool found = false;
            for (const auto& space : topology.snapshot().spaces)
            {
                for (const auto& tab : space.tabs)
                {
                    if (std::ranges::any_of(tab.panes,
                            [&](const TopologyPane& pane) {
                                return pane.pane_id
                                    == command.route_pane_id;
                            }))
                    {
                        params["space_id"] = space.space_id;
                        params["tab_id"] = tab.tab_id;
                        found = true;
                        break;
                    }
                }
                if (found)
                    break;
            }
            if (!found)
            {
                std::fprintf(stderr,
                    "pane_not_found: The replacement pane was not found.\n");
                return 1;
            }
        }
        const std::string probe_client_id
            = make_server_client_id();
        const auto probe = ServerClient::probe({
            .runtime_directory = server_runtime,
            .client_id = probe_client_id,
            .launch_if_missing = false,
        });
        if (!probe.ready())
        {
            std::fprintf(stderr, "%s: %s\n",
                probe.error_code.empty()
                    ? "server_unavailable"
                    : probe.error_code.c_str(),
                probe.error_message.empty()
                    ? "The Draxul server is unavailable."
                    : probe.error_message.c_str());
            return 1;
        }
        if (std::ranges::find(probe.welcome->capabilities,
                "managed-agent-v2")
            == probe.welcome->capabilities.end())
        {
            std::fprintf(stderr,
                "unsupported_server: The running Draxul server predates in-place agent launch; stop it and retry with this build.\n");
            return 1;
        }
        std::string disconnect_error;
        ServerClient::disconnect(server_runtime,
            probe_client_id, disconnect_error,
            probe.welcome->connection_token);
    }
    bool using_global_server
        = command.method == "ui.list"
        || (supports_headless_server
        && (!command.server_runtime_directory.empty()
            || command.replace_pane));
    const auto request
        = [&](const nlohmann::json& request_params) {
              if (!using_global_server)
              {
                  auto local = ControlClient::request(
                      command.control_id, runtime,
                      command.method, request_params);
                  if (local.ok
                      || !supports_headless_server
                      || (local.error_code
                              != "endpoint_unavailable"
                          && (mutating_agent_request
                              || local.error_code
                                  != "io_error")))
                  {
                      return local;
                  }
                  using_global_server = true;
              }
              nlohmann::json global_params
                  = request_params;
              global_params["session_id"]
                  = command.session_id.empty()
                  ? "default"
                  : command.session_id;
              return ControlClient::request(
                  namespaced_control_id(
                      kServerControlId, server_runtime),
                  server_runtime, command.method,
                  std::move(global_params));
          };
    const auto route_ui_request = [&]() -> ControlClientResult {
        auto local = ControlClient::request(command.control_id,
            runtime, command.method, params);
        if (local.ok || command.control_id_explicit
            || (local.error_code != "endpoint_unavailable"
                && local.error_code != "io_error"))
        {
            return local;
        }

        auto routes = ControlClient::request(
            namespaced_control_id(kServerControlId, server_runtime),
            server_runtime, "ui.list",
            { { "session_id", command.session_id.empty()
                                    ? "default"
                                    : command.session_id } });
        if (!routes.ok)
            return routes;
        if (!routes.result.is_array() || routes.result.empty())
        {
            return { false, nullptr, "ui_unavailable",
                "No attached Draxul UI has published a control route for this Session." };
        }
        if (command.method == "pane.focus" && routes.result.size() > 1)
        {
            return { false, routes.result, "ambiguous_ui",
                "More than one Draxul UI is attached; choose one with --ui <control-id> (see 'draxul ui list --json')." };
        }

        const auto dispatch = [&](const nlohmann::json& route) {
            if (!route.is_object()
                || !route.contains("control_id")
                || !route["control_id"].is_string()
                || !route.contains("control_runtime_directory")
                || !route["control_runtime_directory"].is_string())
            {
                return ControlClientResult{ false, nullptr,
                    "invalid_ui_route",
                    "The server returned an invalid UI control route." };
            }
            return ControlClient::request(
                route["control_id"].get<std::string>(),
                route["control_runtime_directory"].get<std::string>(),
                command.method, params);
        };
        if (command.method == "pane.focus")
            return dispatch(routes.result.front());

        std::vector<std::future<ControlClientResult>> pending;
        pending.reserve(routes.result.size());
        for (const auto& route : routes.result)
        {
            pending.push_back(std::async(std::launch::async,
                [&, route] { return dispatch(route); }));
        }
        nlohmann::json dispatched = nlohmann::json::array();
        std::optional<ControlClientResult> first_failure;
        for (size_t index = 0; index < pending.size(); ++index)
        {
            auto routed = pending[index].get();
            if (routed.ok)
            {
                dispatched.push_back({
                    { "control_id",
                        routes.result[index].value(
                            "control_id", std::string{}) },
                    { "result", std::move(routed.result) },
                });
            }
            else if (!first_failure)
                first_failure = std::move(routed);
        }
        if (dispatched.empty())
        {
            return first_failure.value_or(ControlClientResult{
                false, nullptr, "ui_unavailable",
                "No attached Draxul UI accepted the pane action." });
        }
        return { true,
            {
                { "pane_id", command.value },
                { "action", command.action },
                { "dispatched_routes", std::move(dispatched) },
            },
            {}, {} };
    };
    auto result = (command.method == "pane.focus"
                      || command.method == "pane.action")
        ? route_ui_request()
        : request(params);
    if (result.ok && command.method == "agent.wait")
    {
        const auto deadline = command.timeout_ms > 0
            ? std::chrono::steady_clock::now()
                + std::chrono::milliseconds(command.timeout_ms)
            : std::chrono::steady_clock::time_point::max();
        if (result.result.contains("agent"))
        {
            params["runtime_generation"] = result.result["agent"].value("runtime_generation", 0ull);
        }
        while (result.ok && !result.result.value("complete", false)
            && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            result = request(params);
        }
        if (result.ok && !result.result.value("complete", false))
        {
            result.ok = false;
            result.error_code = "timeout";
            result.error_message = "Agent wait timed out.";
        }
    }
    if (!result.ok)
    {
        std::fprintf(stderr, "%s: %s\n",
            result.error_code.c_str(), result.error_message.c_str());
        return 1;
    }
    if (command.json)
        std::printf("%s\n", result.result.dump(2).c_str());
    else
        print_human(command, result.result);
    return 0;
}

} // namespace draxul
