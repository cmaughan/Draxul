#include "control_cli.h"

#include <draxul/config_document.h>
#include <draxul/control_plane.h>

#include <charconv>
#include <cstdio>
#include <nlohmann/json.hpp>

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
    else if (noun == "agent" && verb == "list")
        command.method = "agent.list";
    else if (noun == "agent" && verb == "get")
        command.method = "agent.get";
    else if (noun == "agent" && verb == "explain")
        command.method = "agent.explain";
    else if (noun == "pane" && verb == "read")
        command.method = "pane.read";
    else
    {
        parsed.error = usage();
        return parsed;
    }

    const bool needs_value =
        command.method == "space.get" || command.method == "agent.get"
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
    if (command.method == "space.get" && !parse_int(command.value))
    {
        parsed.error = "Space id must be an integer.";
        return parsed;
    }
    parsed.command = std::move(command);
    return parsed;
}

int run_control_cli(const ControlCliCommand& command)
{
    nlohmann::json params = nlohmann::json::object();
    if (command.method == "space.get")
        params["id"] = *parse_int(command.value);
    else if (command.method == "agent.get" || command.method == "agent.explain")
        params["instance_id"] = command.value;
    else if (command.method == "pane.read")
    {
        params["pane_id"] = command.value;
        params["lines"] = command.lines;
    }

    const auto result = ControlClient::request(command.session_id,
        control_runtime_directory(ConfigDocument::default_path().parent_path()),
        command.method, std::move(params));
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
