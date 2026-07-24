#include "control_cli.h"

#include <draxul/config_document.h>
#include <draxul/control_plane.h>

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
            std::printf("%c %-20s %-10s %-8s space=%d tab=%d pane=%s\n",
                agent.value("focused", false) ? '*' : ' ',
                agent.value("instance_id", "").c_str(),
                agent.value("kind", "").c_str(),
                agent.value("status", "").c_str(),
                route.value("space_id", -1),
                route.value("tab_id", -1),
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
        || command.method == "agent.explain" || command.method == "pane.read";
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

    const auto runtime =
        control_runtime_directory(ConfigDocument::default_path().parent_path());
    auto result = ControlClient::request(
        command.session_id, runtime, command.method, params);
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
            result = ControlClient::request(
                command.session_id, runtime, command.method, params);
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
