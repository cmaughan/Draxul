#include "server_kernel_impl.h"

#include <draxul/topology_layout.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace draxul
{

namespace
{

uint64_t numeric_suffix(std::string_view value)
{
    return topology_id_serial<uint64_t>(value, 0);
}

std::string terminal_display_name(
    const ServerTerminalRuntimeOptions& options)
{
    std::string identity = options.shell_kind;
    if (identity.empty() && !options.command.empty())
    {
        identity = std::filesystem::path(options.command)
                       .stem()
                       .string();
    }
#ifndef _WIN32
    if (identity.empty())
    {
        if (const char* shell = std::getenv("SHELL");
            shell && *shell != '\0')
        {
            identity = std::filesystem::path(shell).stem().string();
        }
    }
#endif
    std::string normalized = identity;
    std::ranges::transform(normalized, normalized.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    if (normalized == "powershell" || normalized == "pwsh")
        return "PowerShell";
    if (normalized == "bash")
        return "Bash";
    if (normalized == "zsh")
        return "Zsh";
    if (normalized == "wsl" || normalized == "wslhost")
        return "WSL";
    if (!identity.empty())
        return identity;
#ifdef _WIN32
    return "PowerShell";
#else
    return "Shell";
#endif
}


} // namespace

bool ServerKernel::Impl::create_server_terminal_with_id(
    std::string_view session_id, std::string terminal_id,
    std::string_view pane_id, std::string_view name,
    std::string& error,
    std::optional<ServerTerminalRuntimeOptions>
        runtime_options,
    bool start_immediately,
    std::string preferred_controller_client_id)
{
    const auto found = sessions.find(std::string(session_id));
    if (found == sessions.end())
    {
        error = "The requested server Session does not exist.";
        return false;
    }
    ServerSession& session = *found->second;
    if (terminal_id.empty() || pane_id.empty())
    {
        error = "Server terminal identity is incomplete.";
        return false;
    }
    if (session.terminals.contains(terminal_id))
    {
        error = "Saved Session contains a duplicate server terminal identity.";
        return false;
    }
    if (server_terminal_count() >= options.max_terminals)
    {
        error = "Draxul server terminal limit reached ("
            + std::to_string(options.max_terminals) + ").";
        return false;
    }
    if (!runtime_options)
    {
        runtime_options = ServerTerminalRuntimeOptions{
            .shell_kind = options.terminal_shell_kind,
            .command = options.terminal_command,
            .args = options.terminal_args,
            .working_directory = options.terminal_working_directory,
            .environment = options.terminal_environment,
            .scrollback_capacity = options.terminal_scrollback_lines,
        };
    }
    const std::string display_name = name.empty()
        ? terminal_display_name(*runtime_options)
        : std::string(name);
    runtime_options->resource_budget
        = terminal_resource_budget;
    const std::weak_ptr<ServerLoopWakeState> weak_loop_wake
        = loop_wake;
    runtime_options->on_output_available
        = [weak_loop_wake] {
              if (const auto state = weak_loop_wake.lock())
              {
                  if (!state->terminal_output_pending.exchange(true))
                      state->condition.notify_one();
              }
          };
    auto runtime = std::make_unique<ServerTerminalRuntime>(
        std::move(*runtime_options));
    ServerTerminalRuntime* runtime_ptr = runtime.get();
    auto service = std::make_unique<RemoteTerminalService>(
        RemoteTerminalServiceOptions{
            .method_prefix = "terminal",
            .server_epoch = epoch_value,
            .pane_id = std::string(pane_id),
            .terminal_id = terminal_id,
            .name = display_name,
            .preferred_controller_client_id
            = std::move(preferred_controller_client_id),
            .loop_latency_warning_threshold
            = options.idle_wait_interval
                + std::chrono::milliseconds(100),
            .prepare_restart_generation
            = [runtime_ptr](uint64_t generation) {
                  runtime_ptr->set_environment_value(
                      "DRAXUL_RUNTIME_GENERATION",
                      std::to_string(generation));
              },
        },
        *runtime);
    if (start_immediately
        && !service->ensure_runtime_started(error))
    {
        return false;
    }
    if (!session.terminals.emplace(
                              terminal_id,
                              ServerTerminalEndpoint{
                                  .runtime = std::move(runtime),
                                  .service = std::move(service),
                              })
            .second)
    {
        error = "Saved Session contains a duplicate server terminal identity.";
        return false;
    }
    session.next_terminal_serial = std::max(
        session.next_terminal_serial, numeric_suffix(terminal_id) + 1);
    return true;
}

size_t ServerKernel::Impl::server_terminal_count() const
{
    size_t count = 0;
    for (const auto& [_, session] : sessions)
        count += session->terminals.size();
    return count;
}


std::optional<std::string>
ServerKernel::Impl::create_server_terminal(
    std::string_view session_id,
    const ServerTerminalTopologyLaunch& launch,
    std::string& error)
{
    const auto found = sessions.find(std::string(session_id));
    if (found == sessions.end())
    {
        error = "The requested server Session does not exist.";
        return std::nullopt;
    }
    ServerSession& session = *found->second;
    const std::string terminal_id
        = "terminal-"
        + std::to_string(session.next_terminal_serial++);
    auto runtime_options = server_terminal_runtime_options(
        session_id, launch.space_id, launch.tab_id,
        launch.pane_id, terminal_id,
        launch.working_directory);
    if (!create_server_terminal_with_id(
            session_id, terminal_id, launch.pane_id,
            launch.name, error, std::move(runtime_options)))
    {
        return std::nullopt;
    }
    return terminal_id;
}

ServerTerminalRuntimeOptions
ServerKernel::Impl::server_terminal_runtime_options(
    std::string_view session_id,
    std::string_view space_id,
    std::string_view tab_id,
    std::string_view pane_id,
    std::string_view terminal_id,
    std::string_view working_directory) const
{
    auto environment = options.terminal_environment;
    const auto set_environment
        = [&environment](std::string key, std::string value) {
              const auto existing = std::ranges::find(
                  environment, key,
                  &std::pair<std::string, std::string>::first);
              if (existing == environment.end())
                  environment.emplace_back(std::move(key), std::move(value));
              else
                  existing->second = std::move(value);
          };
    set_environment("DRAXUL_ENV", "1");
    set_environment("DRAXUL_SESSION_ID", std::string(session_id));
    set_environment("DRAXUL_SPACE_ID", std::string(space_id));
    set_environment("DRAXUL_TAB_ID", std::string(tab_id));
    set_environment("DRAXUL_PANE_ID", std::string(pane_id));
    set_environment("DRAXUL_TERMINAL_ID", std::string(terminal_id));
    set_environment("DRAXUL_SERVER_EPOCH", epoch_value);
    set_environment("DRAXUL_RUNTIME_GENERATION", "1");
    set_environment("DRAXUL_SERVER_RUNTIME_DIR",
        options.runtime_directory.string());
    if (!options.client_executable.empty())
    {
        set_environment("DRAXUL_EXECUTABLE",
            options.client_executable.string());
    }
    return {
        .shell_kind = options.terminal_shell_kind,
        .command = options.terminal_command,
        .args = options.terminal_args,
        .working_directory = working_directory.empty()
            ? options.terminal_working_directory
            : std::string(working_directory),
        .environment = std::move(environment),
        .scrollback_capacity = options.terminal_scrollback_lines,
    };
}

std::optional<std::string>
ServerKernel::Impl::create_managed_agent_terminal(
    std::string_view session_id,
    std::string_view space_id,
    std::string_view tab_id,
    std::string_view pane_id,
    std::string_view name,
    const ManagedAgentTopologyLaunch& launch,
    std::string& error)
{
    const auto found = sessions.find(
        std::string(session_id));
    if (found == sessions.end())
    {
        error = "The requested server Session does not exist.";
        return std::nullopt;
    }
    ServerSession& session = *found->second;
    const std::string terminal_id
        = "terminal-"
        + std::to_string(session.next_terminal_serial++);
    auto runtime_options = managed_agent_runtime_options(
        session_id, space_id, tab_id, pane_id,
        terminal_id, launch, error);
    if (!runtime_options)
        return std::nullopt;
    if (!create_server_terminal_with_id(
            session_id, terminal_id, pane_id, name,
            error, std::move(*runtime_options), true,
            launch.preferred_controller_client_id))
    {
        return std::nullopt;
    }
    return terminal_id;
}

std::optional<ServerTerminalRuntimeOptions>
ServerKernel::Impl::managed_agent_runtime_options(
    std::string_view session_id,
    std::string_view space_id,
    std::string_view tab_id,
    std::string_view pane_id,
    std::string_view terminal_id,
    const ManagedAgentTopologyLaunch& launch,
    std::string& error) const
{
    const AgentDefinition* definition
        = agent_definitions.find(
            launch.identity.profile_id);
    if (!definition
        || definition->kind != launch.identity.kind)
    {
        error = "Managed agent profile is unavailable in the server.";
        return std::nullopt;
    }
    if (launch.additional_args.size() > 64
        || std::ranges::any_of(
            launch.additional_args,
            [](const std::string& value) {
                return value.size() > 4096;
            }))
    {
        error = "Managed agent arguments exceed server limits.";
        return std::nullopt;
    }

    std::vector<std::string> args
        = launch.replace_default_args
        ? std::vector<std::string>{}
        : definition->default_args;
    args.insert(args.end(),
        launch.additional_args.begin(),
        launch.additional_args.end());

    auto environment = options.terminal_environment;
    const auto set_environment
        = [&environment](
              std::string key, std::string value) {
              const auto existing = std::ranges::find(
                  environment, key,
                  &std::pair<std::string,
                      std::string>::first);
              if (existing == environment.end())
              {
                  environment.emplace_back(
                      std::move(key), std::move(value));
              }
              else
              {
                  existing->second = std::move(value);
              }
          };
    set_environment("DRAXUL_ENV", "1");
    set_environment(
        "DRAXUL_SESSION_ID", std::string(session_id));
    set_environment(
        "DRAXUL_SPACE_ID", std::string(space_id));
    set_environment(
        "DRAXUL_TAB_ID", std::string(tab_id));
    set_environment(
        "DRAXUL_PANE_ID", std::string(pane_id));
    set_environment(
        "DRAXUL_TERMINAL_ID",
        std::string(terminal_id));
    set_environment("DRAXUL_AGENT_INSTANCE_ID",
        launch.identity.instance_id);
    set_environment(
        "DRAXUL_AGENT", launch.identity.kind);
    set_environment(
        "DRAXUL_SERVER_EPOCH", epoch_value);
    set_environment(
        "DRAXUL_RUNTIME_GENERATION", "1");
    set_environment("DRAXUL_SERVER_RUNTIME_DIR",
        options.runtime_directory.string());
    if (!options.client_executable.empty())
    {
        set_environment("DRAXUL_EXECUTABLE",
            options.client_executable.string());
    }

    return ServerTerminalRuntimeOptions{
        .command = definition->executable,
        .args = std::move(args),
        .working_directory
        = launch.working_directory.empty()
            ? options.terminal_working_directory
            : launch.working_directory,
        .environment = std::move(environment),
        .scrollback_capacity
        = options.terminal_scrollback_lines,
    };
}

void ServerKernel::Impl::destroy_server_terminal(
    std::string_view session_id, std::string_view terminal_id)
{
    const auto session = sessions.find(std::string(session_id));
    if (session != sessions.end())
        session->second->terminals.erase(std::string(terminal_id));
}

bool ServerKernel::Impl::restart_server_terminal(
    std::string_view session_id, std::string_view terminal_id,
    std::string& error)
{
    const auto session = sessions.find(std::string(session_id));
    if (session == sessions.end())
    {
        error = "The requested server Session does not exist.";
        return false;
    }
    const auto terminal
        = session->second->terminals.find(std::string(terminal_id));
    if (terminal == session->second->terminals.end())
    {
        error = "The requested server terminal does not exist.";
        return false;
    }
    return terminal->second.service->restart_runtime(error);
}

void ServerKernel::Impl::refresh_agents(
    ServerSession& session,
    std::chrono::steady_clock::time_point now)
{
    if (!session.topology_service || !session.agent_service)
        return;
    std::vector<ServerAgentRuntimeView> runtimes;
    const TopologySnapshot& topology
        = session.topology_service->snapshot();
    for (const auto& space : topology.spaces)
    {
        for (const auto& tab : space.tabs)
        {
            for (const auto& pane : tab.panes)
            {
                if (pane.domain
                    != TopologyPaneDomain::ServerTerminal)
                {
                    continue;
                }
                const auto terminal
                    = session.terminals.find(pane.terminal_id);
                if (terminal == session.terminals.end())
                    continue;
                const auto& endpoint = terminal->second;
                const bool running
                    = endpoint.runtime->is_running();
                runtimes.push_back({
                    .space_id = space.space_id,
                    .tab_id = tab.tab_id,
                    .pane_id = pane.pane_id,
                    .terminal_id = pane.terminal_id,
                    .declared_identity = pane.agent,
                    .session_ref = pane.agent_session,
                    .generation = {
                        endpoint.service->generation(),
                    },
                    .runtime_running = running,
                    .exit_code = endpoint.runtime->exit_code(),
                    .process_observation = running && !pane.agent ? endpoint.runtime->capture_agent_process_observation() : std::nullopt,
                    .terminal_observation = running ? endpoint.runtime->capture_agent_observation(12, 8 * 1024) : std::nullopt,
                });
            }
        }
    }
    session.agent_service->update(runtimes, now);
}


} // namespace draxul
