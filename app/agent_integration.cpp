#include "agent_integration.h"

#include <draxul/agent_integration.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>

namespace draxul
{

namespace
{

std::optional<std::filesystem::path> integration_directory(
    AgentIntegrationProvider provider)
{
    const char* explicit_home = provider == AgentIntegrationProvider::Codex
        ? std::getenv("CODEX_HOME")
        : std::getenv("CLAUDE_CONFIG_DIR");
    if (explicit_home && *explicit_home)
        return std::filesystem::path(explicit_home);
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (!home || !*home)
        return std::nullopt;
    return std::filesystem::path(home)
        / (provider == AgentIntegrationProvider::Codex ? ".codex" : ".claude");
}

std::optional<AgentIntegrationProvider> integration_provider(
    std::string_view name)
{
    if (name == "codex")
        return AgentIntegrationProvider::Codex;
    if (name == "claude")
        return AgentIntegrationProvider::Claude;
    return std::nullopt;
}

AgentIntegrationStatus status_for(AgentIntegrationProvider provider)
{
    const auto directory = integration_directory(provider);
    if (!directory)
    {
        return {
            .provider = provider,
            .state = AgentIntegrationState::Invalid,
            .reason = provider == AgentIntegrationProvider::Codex
                ? "Codex home cannot be resolved."
                : "Claude config directory cannot be resolved.",
        };
    }
    return inspect_agent_integration(
        provider, agent_integration_paths(provider, *directory));
}

nlohmann::json status_json(const AgentIntegrationStatus& status)
{
    nlohmann::json result = {
        { "target", agent_integration_provider_name(status.provider) },
        { "state", agent_integration_state_name(status.state) },
    };
    if (status.expected_version != 0)
        result["expected_version"] = status.expected_version;
    if (!status.path.empty())
        result["path"] = status.path.string();
    if (!status.reason.empty())
        result["reason"] = status.reason;
    return result;
}

void print_status(const AgentIntegrationStatus& status, bool json)
{
    if (json)
        std::printf("%s\n", status_json(status).dump(2).c_str());
    else
        std::printf("%s: %s\n",
            agent_integration_provider_name(status.provider),
            agent_integration_state_name(status.state));
}

} // namespace

ParseIntegrationCliResult parse_integration_cli(
    const std::vector<std::string>& args)
{
    ParseIntegrationCliResult result;
    if (args.size() < 2 || args[1] != "integration")
        return result;
    result.recognized = true;
    if (args.size() < 3
        || (args[2] != "status" && args[2] != "install"
            && args[2] != "uninstall"))
    {
        result.error = "Usage: draxul integration <status|install|uninstall> "
                       "[codex|claude] [--json]";
        return result;
    }
    IntegrationCliCommand command;
    command.action = args[2];
    size_t position = 3;
    if (position < args.size() && !args[position].starts_with("--"))
        command.target = args[position++];
    if (command.action != "status" && command.target.empty())
    {
        result.error = "Integration install/uninstall requires a target.";
        return result;
    }
    if (!command.target.empty() && !integration_provider(command.target))
    {
        result.error = "Integration target must be codex or claude.";
        return result;
    }
    while (position < args.size())
    {
        if (args[position++] == "--json")
            command.json = true;
        else
        {
            result.error = "Unknown integration option.";
            return result;
        }
    }
    result.command = std::move(command);
    return result;
}

int run_integration_cli(const IntegrationCliCommand& command)
{
    if (command.action != "status")
    {
        const auto provider = integration_provider(command.target);
        const auto directory = provider ? integration_directory(*provider)
                                        : std::nullopt;
        if (!provider || !directory)
        {
            std::string error;
            if (command.action == "install")
            {
                error = command.target == "claude"
                    ? "Claude config directory was not found. Install Claude first."
                    : "Codex config directory was not found. Install Codex first.";
            }
            else
            {
                error = command.target == "claude"
                    ? "Claude config directory cannot be resolved."
                    : "Codex home cannot be resolved.";
            }
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        const auto result = apply_agent_integration({
            .provider = *provider,
            .action = command.action == "install"
                ? AgentIntegrationAction::Install
                : AgentIntegrationAction::Uninstall,
            .paths = agent_integration_paths(*provider, *directory),
        });
        if (!result.success)
        {
            std::fprintf(stderr, "%s\n", result.error.c_str());
            return 1;
        }
        print_status(result.status, command.json);
        return 0;
    }

    if (command.target.empty())
    {
        const nlohmann::json statuses = nlohmann::json::array({
            status_json(status_for(AgentIntegrationProvider::Codex)),
            status_json(status_for(AgentIntegrationProvider::Claude)),
        });
        if (command.json)
            std::printf("%s\n", statuses.dump(2).c_str());
        else
        {
            for (const auto provider : { AgentIntegrationProvider::Codex,
                     AgentIntegrationProvider::Claude })
            {
                const auto status = status_for(provider);
                std::printf("%s: %s\n",
                    agent_integration_provider_name(provider),
                    agent_integration_state_name(status.state));
            }
        }
        return 0;
    }

    print_status(status_for(*integration_provider(command.target)), command.json);
    return 0;
}

} // namespace draxul
