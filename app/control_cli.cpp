#include "control_cli.h"

#include <draxul/config_document.h>
#include <draxul/control_plane.h>
#include <draxul/server_client.h>

#include <charconv>
#include <chrono>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <thread>

namespace draxul
{

namespace
{

std::optional<int> parse_int(std::string_view text)
{
    int result = 0;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return std::nullopt;
    return result;
}

std::optional<uint64_t> parse_uint64(std::string_view text)
{
    uint64_t result = 0;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return std::nullopt;
    return result;
}

std::string usage()
{
    return "Usage: draxul <space|agent|pane> <command> [value] "
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
    std::printf("%s\n", result.dump(2).c_str());
}

} // namespace

ParseControlCliResult parse_control_cli(const std::vector<std::string>& args)
{
    ParseControlCliResult parsed;
    if (args.size() < 2
        || (args[1] != "space" && args[1] != "agent" && args[1] != "pane"))
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
    else if (noun == "agent" && verb == "send")
        command.method = "agent.send_text";
    else if (noun == "agent" && verb == "keys")
        command.method = "agent.send_keys";
    else if (noun == "agent" && verb == "wait")
        command.method = "agent.wait";
    else if (noun == "pane" && verb == "read")
        command.method = "pane.read";
    else if (noun == "pane" && verb == "report-agent-session")
        command.method = "pane.report_agent_session";
    else
    {
        parsed.error = usage();
        return parsed;
    }

    const bool needs_value =
        command.method == "space.get" || command.method == "space.focus"
        || command.method == "agent.get" || command.method == "agent.start"
        || command.method == "agent.focus" || command.method == "agent.restart"
        || command.method == "agent.send_text"
        || command.method == "agent.send_keys" || command.method == "agent.wait"
        || command.method == "agent.explain" || command.method == "pane.read"
        || command.method == "pane.report_agent_session";
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
                parsed.error = "--space requires an integer id.";
                return parsed;
            }
            command.space_id = parse_int(args[position++]);
            if (!command.space_id)
            {
                parsed.error = "--space requires an integer id.";
                return parsed;
            }
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
        parsed.error =
            "pane report-agent-session requires --agent-instance, --source, "
            "--agent, --integration-version, --sequence, and --session-ref.";
        return parsed;
    }
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
    else if (command.method == "agent.start")
    {
        params["profile_id"] = command.value;
        params["args"] = command.arguments;
        if (!command.working_directory.empty())
            params["cwd"] = command.working_directory;
        if (command.space_id)
            params["space_id"] = *command.space_id;
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
    }

    const auto runtime =
        control_runtime_directory(ConfigDocument::default_path().parent_path());
    const auto server_runtime = server_runtime_directory(
        ConfigDocument::default_path().parent_path());
    const bool supports_headless_server
        = command.method == "agent.list"
        || command.method == "agent.get"
        || command.method == "agent.explain"
        || command.method == "agent.restart"
        || command.method == "agent.send_text"
        || command.method == "agent.send_keys"
        || command.method == "agent.wait"
        || command.method == "pane.read";
    bool using_global_server = false;
    const auto request
        = [&](const nlohmann::json& request_params) {
              if (!using_global_server)
              {
                  auto local = ControlClient::request(
                      command.session_id, runtime,
                      command.method, request_params);
                  if (local.ok
                      || !supports_headless_server
                      || (local.error_code
                              != "endpoint_unavailable"
                          && local.error_code != "io_error"))
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
    auto result = request(params);
    if (result.ok && command.method == "agent.wait")
    {
        const auto deadline = command.timeout_ms > 0
            ? std::chrono::steady_clock::now()
                + std::chrono::milliseconds(command.timeout_ms)
            : std::chrono::steady_clock::time_point::max();
        if (result.result.contains("agent"))
        {
            params["runtime_generation"] =
                result.result["agent"].value("runtime_generation", 0ull);
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
