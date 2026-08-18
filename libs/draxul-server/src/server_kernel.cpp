#include "server_kernel_impl.h"

#include "fake_terminal_runtime.h"
#include "remote_terminal_service.h"
#include "server_terminal_runtime.h"
#include "session_poll_service.h"
#include "session_topology_bridge.h"
#include "topology_service.h"

#include <draxul/topology_layout.h>

#include <draxul/control_plane.h>
#include <draxul/log.h>
#include <draxul/process_util.h>
#include <draxul/remote_terminal_protocol.h>
#include <draxul/server_protocol.h>
#include <draxul/session_state.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/types.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace draxul
{

namespace
{

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


uint64_t numeric_suffix(std::string_view value)
{
    return topology_id_serial<uint64_t>(value, 0);
}

bool valid_server_session_id(std::string_view value)
{
    return !value.empty()
        && value.size() <= kServerMaxSessionIdBytes
        && !std::ranges::any_of(value,
            [](unsigned char ch) {
                return ch < 0x20 || ch == 0x7f;
            });
}


std::optional<std::filesystem::path> archive_corrupt_checkpoint(
    const std::filesystem::path& path, std::string& error)
{
    const std::string stamp
        = std::to_string(current_unix_time_ms());
    for (size_t suffix = 0; suffix < 1000; ++suffix)
    {
        std::filesystem::path archived = path;
        archived += ".corrupt-" + stamp;
        if (suffix > 0)
            archived += "-" + std::to_string(suffix);
        std::error_code exists_error;
        if (std::filesystem::exists(archived, exists_error))
            continue;
        if (exists_error)
        {
            error = exists_error.message();
            return std::nullopt;
        }
        std::error_code rename_error;
        std::filesystem::rename(path, archived, rename_error);
        if (!rename_error)
            return archived;
        error = rename_error.message();
        return std::nullopt;
    }
    error = "Unable to choose a unique corrupt-checkpoint archive name.";
    return std::nullopt;
}

} // namespace

std::filesystem::path server_session_state_path(
    const std::filesystem::path& runtime_directory)
{
    return runtime_directory / "sessions" / "default.toml";
}

std::filesystem::path server_session_state_path(
    const std::filesystem::path& runtime_directory,
    std::string_view session_id)
{
    if (session_id.empty() || session_id == "default")
        return server_session_state_path(runtime_directory);
    return runtime_directory / "sessions"
        / session_state_file_name(session_id);
}




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
    const bool legacy_generated_name = name == "Server Shell";
    const std::string display_name = name.empty() || legacy_generated_name
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

void ServerKernel::Impl::reset_services()
{
    for (auto& [session_id, session] : sessions)
    {
        (void)session_id;
        session->topology_service.reset();
        session->agent_service.reset();
        session->poll_service.reset();
        session->terminals.clear();
        session->next_terminal_serial = 2;
    }
    fake_terminal_service.reset();
    fake_terminal.reset();
}

bool ServerKernel::Impl::prepare_session_restore(std::string&)
{
    sessions.clear();
    unassigned_restore_warnings.clear();

    auto prepare = [this](std::filesystem::path path,
                       std::optional<std::string> expected_session_id) {
        auto prepared = std::make_unique<ServerSession>();
        prepared->persistence_path = std::move(path);
        prepared->checkpoint_file_present
            = std::filesystem::exists(prepared->persistence_path);
        prepared->checkpoint_state = prepared->checkpoint_file_present
            ? "restoring"
            : "pending";

        std::optional<SessionSnapshot> saved;
        std::string load_error;
        if (prepared->checkpoint_file_present)
        {
            saved = load_session_state_from_path(
                prepared->persistence_path, &load_error);
        }

        if (expected_session_id)
            prepared->session_id = *expected_session_id;
        else if (saved)
            prepared->session_id = saved->session_id;

        if (prepared->checkpoint_file_present && !saved
            && !expected_session_id)
        {
            std::string archive_error;
            const auto archived = archive_corrupt_checkpoint(
                prepared->persistence_path, archive_error);
            unassigned_restore_warnings.push_back(
                archived
                    ? "Archived unreadable Session checkpoint as '"
                        + archived->filename().string() + "': "
                        + (load_error.empty()
                                ? "the checkpoint is invalid."
                                : load_error)
                    : "Could not archive unreadable Session checkpoint '"
                        + prepared->persistence_path.filename().string()
                        + "': " + archive_error);
            return;
        }

        if (!valid_server_session_id(prepared->session_id))
        {
            unassigned_restore_warnings.push_back(
                "Skipped Session checkpoint with an invalid identity: "
                + prepared->persistence_path.string());
            return;
        }

        if (saved && expected_session_id
            && saved->session_id != *expected_session_id)
        {
            load_error = "Saved Session identity does not match its checkpoint path.";
            saved.reset();
        }

        if (prepared->checkpoint_file_present && !saved)
        {
            const std::string restore_error = load_error.empty()
                ? "Saved Session could not be restored."
                : load_error;
            prepared->restore_warnings.push_back(
                restore_error);
            std::string archive_error;
            const auto archived = archive_corrupt_checkpoint(
                prepared->persistence_path, archive_error);
            if (archived)
            {
                prepared->checkpoint_file_present = false;
                prepared->checkpoint_state = "recovered";
                prepared->restore_warnings.push_back(
                    "Archived the unreadable checkpoint as '"
                    + archived->filename().string()
                    + "'; future checkpoints will continue.");
            }
            else
            {
                prepared->checkpoint_state = "recovery_pending";
                prepared->checkpoint_error
                    = "Unable to archive the unreadable checkpoint: "
                    + archive_error;
                prepared->corrupt_checkpoint_archive_required
                    = true;
                prepared->restore_warnings.push_back(
                    prepared->checkpoint_error
                    + ". Draxul will retry before saving.");
            }
        }
        else if (saved)
        {
            auto restored
                = restore_session_topology(*saved, load_error);
            if (!restored)
            {
                const std::string restore_error = load_error.empty()
                    ? "Saved Session could not be restored."
                    : load_error;
                prepared->restore_warnings.push_back(
                    restore_error);
                std::string archive_error;
                const auto archived = archive_corrupt_checkpoint(
                    prepared->persistence_path, archive_error);
                if (archived)
                {
                    prepared->checkpoint_file_present = false;
                    prepared->checkpoint_state = "recovered";
                    prepared->restore_warnings.push_back(
                        "Archived the unusable checkpoint as '"
                        + archived->filename().string()
                        + "'; future checkpoints will continue.");
                }
                else
                {
                    prepared->checkpoint_state
                        = "recovery_pending";
                    prepared->checkpoint_error
                        = "Unable to archive the unusable checkpoint: "
                        + archive_error;
                    prepared->corrupt_checkpoint_archive_required
                        = true;
                    prepared->restore_warnings.push_back(
                        prepared->checkpoint_error
                        + ". Draxul will retry before saving.");
                }
            }
            else
            {
                prepared->restore_warnings
                    = std::move(restored->warnings);
                if (!prepared->restore_warnings.empty())
                {
                    prepared->checkpoint_state
                        = "restored_with_warnings";
                }
                else
                {
                    prepared->checkpoint_state = "restored";
                }
                prepared->restored_topology
                    = std::move(restored->topology);
            }
        }
        if (saved)
            prepared->session_name = saved->session_name;
        if (prepared->session_name.empty())
            prepared->session_name = prepared->session_id;

        const std::string session_id = prepared->session_id;
        if (!sessions.emplace(session_id, std::move(prepared)).second)
        {
            unassigned_restore_warnings.push_back(
                "Skipped duplicate Session checkpoint for '"
                + session_id + "'.");
            return;
        }
        const ServerSession& stored = *sessions.at(session_id);
        if (!stored.restore_warnings.empty())
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "Draxul server retained Session checkpoint %s with restore warnings: %s",
                stored.persistence_path.string().c_str(),
                stored.restore_warnings.front().c_str());
        }
    };

    const std::filesystem::path default_path
        = options.session_state_file.empty()
        ? server_session_state_path(options.runtime_directory)
        : options.session_state_file;
    prepare(default_path, std::string("default"));

    const std::filesystem::path sessions_directory
        = options.runtime_directory / "sessions";
    std::vector<std::filesystem::path> saved_paths;
    std::error_code iteration_error;
    for (std::filesystem::directory_iterator it(
             sessions_directory, iteration_error);
        !iteration_error
        && it != std::filesystem::directory_iterator();
        it.increment(iteration_error))
    {
        if (it->is_regular_file()
            && it->path().extension() == ".toml"
            && it->path().lexically_normal()
                != default_path.lexically_normal())
        {
            saved_paths.push_back(it->path());
        }
    }
    std::ranges::sort(saved_paths);
    const size_t named_session_limit
        = kServerMaxSessions > 0
        ? kServerMaxSessions - 1
        : 0;
    if (saved_paths.size() > named_session_limit)
    {
        unassigned_restore_warnings.push_back(
            "Skipped "
            + std::to_string(
                saved_paths.size() - named_session_limit)
            + " Session checkpoints beyond the server limit.");
        saved_paths.resize(named_session_limit);
    }
    for (const auto& path : saved_paths)
        prepare(path, std::nullopt);
    return true;
}

bool ServerKernel::Impl::initialize_session(
    std::string_view session_id, std::string& error)
{
    const auto found = sessions.find(std::string(session_id));
    if (found == sessions.end())
    {
        error = "The requested server Session does not exist.";
        return false;
    }
    ServerSession& session = *found->second;
    session.topology_service.reset();
    session.agent_service.reset();
    session.poll_service.reset();
    session.terminals.clear();
    session.next_terminal_serial = 2;
    session.next_agent_serial = 1;

    const std::string stable_session_id = session.session_id;
    TopologyServiceCallbacks callbacks{
        .create_server_terminal
        = [this, stable_session_id](
              const ServerTerminalTopologyLaunch& launch,
              std::string& callback_error) { return create_server_terminal(
                                                 stable_session_id, launch, callback_error); },
        .destroy_server_terminal
        = [this, stable_session_id](std::string_view terminal_id) { destroy_server_terminal(
                                                                        stable_session_id, terminal_id); },
        .restart_server_terminal
        = [this, stable_session_id](std::string_view terminal_id,
              std::string& callback_error) { return restart_server_terminal(stable_session_id,
                                                 terminal_id, callback_error); },
        .create_managed_agent_terminal
        = [this, stable_session_id](
              std::string_view space_id,
              std::string_view tab_id,
              std::string_view pane_id,
              std::string_view name,
              const ManagedAgentTopologyLaunch& launch,
              std::string& callback_error) { return create_managed_agent_terminal(
                                                 stable_session_id, space_id, tab_id,
                                                 pane_id, name, launch,
                                                 callback_error); },
    };

    if (session.restored_topology)
    {
        for (const auto& space : session.restored_topology->spaces)
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
                    std::optional<
                        ServerTerminalRuntimeOptions>
                        runtime_options;
                    bool start_immediately = false;
                    if (pane.agent)
                    {
                        session.next_agent_serial = std::max(
                            session.next_agent_serial,
                            numeric_suffix(
                                pane.agent->instance_id)
                                + 1);
                    }
                    if (pane.agent
                        && pane.agent->origin
                            == AgentIdentityOrigin::Managed
                        && pane.restore_policy
                            != AgentRestorePolicy::ShellOnly)
                    {
                        ManagedAgentTopologyLaunch launch{
                            .identity = *pane.agent,
                            .restore_policy
                            = pane.restore_policy,
                            .working_directory
                            = pane.server_working_directory
                                    .empty()
                                ? space.root_directory
                                : pane.server_working_directory,
                        };
                        if (options.agents_resume_on_restore
                            && pane.restore_policy
                                == AgentRestorePolicy::
                                    ResumeIfAvailable
                            && pane.agent_session
                            && pane.agent_session->kind
                                == AgentSessionRefKind::Id
                            && is_official_agent_session_source(
                                pane.agent_session->source,
                                pane.agent->kind))
                        {
                            launch.replace_default_args = true;
                            launch.additional_args
                                = pane.agent->kind == "codex"
                                ? std::vector<std::string>{
                                      "resume",
                                      pane.agent_session->value
                                  }
                                : std::vector<std::string>{ "--resume", pane.agent_session->value };
                        }
                        runtime_options
                            = managed_agent_runtime_options(
                                stable_session_id,
                                space.space_id,
                                tab.tab_id,
                                pane.pane_id,
                                pane.terminal_id,
                                launch, error);
                        if (runtime_options)
                        {
                            start_immediately = true;
                        }
                        else
                        {
                            session.restore_warnings.push_back(
                                "Restored agent pane '"
                                + pane.pane_id
                                + "' as a shell: " + error);
                            error.clear();
                        }
                    }
                    if (!runtime_options)
                    {
                        runtime_options
                            = server_terminal_runtime_options(
                                stable_session_id,
                                space.space_id,
                                tab.tab_id,
                                pane.pane_id,
                                pane.terminal_id,
                                pane.server_working_directory.empty()
                                    ? space.root_directory
                                    : pane.server_working_directory);
                    }
                    bool created
                        = create_server_terminal_with_id(
                            stable_session_id,
                            pane.terminal_id,
                            pane.pane_id, pane.name, error,
                            std::move(runtime_options),
                            start_immediately);
                    if (!created && start_immediately)
                    {
                        session.restore_warnings.push_back(
                            "Restored agent pane '"
                            + pane.pane_id
                            + "' as a shell after its agent failed to start: "
                            + error);
                        error.clear();
                        created
                            = create_server_terminal_with_id(
                                stable_session_id,
                                pane.terminal_id,
                                pane.pane_id, pane.name,
                                error,
                                server_terminal_runtime_options(
                                    stable_session_id,
                                    space.space_id,
                                    tab.tab_id,
                                    pane.pane_id,
                                    pane.terminal_id,
                                    pane.server_working_directory.empty()
                                        ? space.root_directory
                                        : pane.server_working_directory));
                    }
                    if (!created)
                    {
                        session.terminals.clear();
                        return false;
                    }
                }
            }
        }
        session.topology_service
            = std::make_unique<TopologyService>(
                *session.restored_topology,
                std::move(callbacks));
        session.last_checkpoint_revision
            = session.topology_service->snapshot().revision;
    }
    else
    {
        session.topology_service
            = std::make_unique<TopologyService>(
                stable_session_id, callbacks);
        const TopologySnapshot& initial
            = session.topology_service->snapshot();
        const TopologySpace& initial_space
            = initial.spaces.front();
        const TopologyTab& initial_tab
            = initial_space.tabs.front();
        const TopologyPane& initial_pane
            = initial_tab.panes.front();
        if (!create_server_terminal_with_id(stable_session_id,
                std::string(kServerShellTerminalId),
                kServerShellPaneId, {}, error,
                server_terminal_runtime_options(
                    stable_session_id,
                    initial_space.space_id,
                    initial_tab.tab_id,
                    initial_pane.pane_id,
                    initial_pane.terminal_id,
                    initial_space.root_directory)))
        {
            session.terminals.clear();
            session.topology_service.reset();
            return false;
        }
    }
    session.next_checkpoint_at = std::chrono::steady_clock::now()
        + options.session_checkpoint_interval;
    session.agent_service
        = std::make_unique<ServerAgentService>(
            stable_session_id);
    session.poll_service
        = std::make_unique<SessionPollService>(epoch_value);
    session.next_agent_refresh_at
        = std::chrono::steady_clock::now();
    return true;
}

ServerKernel::Impl::ServerSession*
ServerKernel::Impl::ensure_session(
    std::string_view session_id, std::string& error)
{
    const auto found = sessions.find(std::string(session_id));
    if (found != sessions.end())
        return found->second.get();
    if (sessions.size() >= kServerMaxSessions)
    {
        error = "The Draxul server Session limit has been reached.";
        return nullptr;
    }

    auto session = std::make_unique<ServerSession>();
    session->session_id = std::string(session_id);
    session->session_name = session->session_id;
    session->persistence_path = server_session_state_path(
        options.runtime_directory, session_id);
    const std::string key = session->session_id;
    sessions.emplace(key, std::move(session));
    if (!initialize_session(key, error))
    {
        sessions.erase(key);
        return nullptr;
    }
    return sessions.at(key).get();
}

ServerKernel::Impl::ServerSession*
ServerKernel::Impl::resolve_session(const nlohmann::json& params,
    SessionServiceNeed need, std::string_view unavailable_message,
    ControlMethodResult& failure)
{
    std::string session_id;
    std::string session_error;
    if (!read_session_id(params, session_id, session_error))
    {
        failure = ControlMethodResult::error(
            "invalid_session", std::move(session_error));
        return nullptr;
    }
    ServerSession* session = ensure_session(session_id, session_error);
    const bool needs_topology
        = need == SessionServiceNeed::Topology
        || need == SessionServiceNeed::TopologyAndAgent;
    const bool needs_agent = need == SessionServiceNeed::Agent
        || need == SessionServiceNeed::TopologyAndAgent;
    if (!session
        || (needs_topology && !session->topology_service)
        || (needs_agent && !session->agent_service))
    {
        failure = ControlMethodResult::error("session_unavailable",
            session_error.empty()
                ? std::string(unavailable_message)
                : std::move(session_error));
        return nullptr;
    }
    return session;
}

ControlMethodResult ServerKernel::Impl::delete_session(
    const nlohmann::json& params)
{
    if (!params.is_object()
        || !params.contains("session_id")
        || !params["session_id"].is_string())
    {
        return ControlMethodResult::error(
            "invalid_session",
            "Session deletion requires a string session_id.");
    }
    const std::string session_id
        = params["session_id"].get<std::string>();
    if (!valid_server_session_id(session_id))
    {
        return ControlMethodResult::error(
            "invalid_session",
            "The requested server Session identity is invalid.");
    }
    const auto confirmation
        = params.find("confirm_live_terminals");
    if (confirmation != params.end()
        && !confirmation->is_boolean())
    {
        return ControlMethodResult::error(
            "invalid_params",
            "confirm_live_terminals must be a boolean.");
    }
    const auto found = sessions.find(session_id);
    if (found == sessions.end())
    {
        return ControlMethodResult::error(
            "session_not_found",
            "The requested server Session does not exist.");
    }
    if (found->second->checkpoint_task)
    {
        return ControlMethodResult::error(
            "checkpoint_busy",
            "The Session checkpoint is still being written. Retry shortly.");
    }

    const size_t attached_clients
        = active_clients_for_session(session_id);
    if (attached_clients > 0)
    {
        return ControlMethodResult::error(
            "session_attached",
            "The Session is still attached to "
                + std::to_string(attached_clients)
                + " Draxul UI"
                + (attached_clients == 1 ? "" : "s")
                + ". Close "
                + (attached_clients == 1 ? "it" : "them")
                + " and retry.");
    }

    const size_t live_terminals
        = static_cast<size_t>(std::ranges::count_if(
            found->second->terminals,
            [](const auto& item) {
                return item.second.runtime->is_running();
            }));
    const bool confirmed
        = confirmation != params.end()
        && confirmation->get<bool>();
    if (live_terminals > 0 && !confirmed)
    {
        return ControlMethodResult::error(
            "confirmation_required",
            "The Session has "
                + std::to_string(live_terminals)
                + " live terminal"
                + (live_terminals == 1 ? "" : "s")
                + ". Retry with --yes to stop "
                  "them and delete the Session.");
    }

    const std::filesystem::path checkpoint_path
        = found->second->persistence_path;
    std::error_code remove_error;
    const bool checkpoint_removed
        = std::filesystem::remove(
            checkpoint_path, remove_error);
    if (remove_error)
    {
        return ControlMethodResult::error(
            "checkpoint_delete_failed",
            "Unable to delete the Session checkpoint: "
                + remove_error.message());
    }

    sessions.erase(found);
    forget_session_clients(session_id);
    return ControlMethodResult::success({
        { "deleted", true },
        { "session_id", session_id },
        { "stopped_live_terminals", live_terminals },
        { "checkpoint_removed", checkpoint_removed },
    });
}

ControlMethodResult ServerKernel::Impl::delete_all_sessions(
    const nlohmann::json& params)
{
    if (!params.is_object())
    {
        return ControlMethodResult::error(
            "invalid_params",
            "Bulk Session deletion requires an object.");
    }
    const auto confirmation
        = params.find("confirm_live_terminals");
    if (confirmation == params.end()
        || !confirmation->is_boolean())
    {
        return ControlMethodResult::error(
            "invalid_params",
            "confirm_live_terminals must be a boolean.");
    }
    if (!confirmation->get<bool>())
    {
        return ControlMethodResult::error(
            "confirmation_required",
            "Bulk Session deletion requires --yes.");
    }

    size_t attached_clients = 0;
    size_t live_terminals = 0;
    std::vector<std::string> session_ids;
    session_ids.reserve(sessions.size());
    for (const auto& [session_id, session] : sessions)
    {
        if (session->checkpoint_task)
        {
            return ControlMethodResult::error(
                "checkpoint_busy",
                "A Session checkpoint is still being written. Retry shortly.");
        }
        attached_clients += active_clients_for_session(
            session_id);
        live_terminals += static_cast<size_t>(
            std::ranges::count_if(
                session->terminals,
                [](const auto& item) {
                    return item.second.runtime->is_running();
                }));
        session_ids.push_back(session_id);
    }
    if (attached_clients > 0)
    {
        return ControlMethodResult::error(
            "session_attached",
            std::to_string(attached_clients)
                + " Draxul UI"
                + (attached_clients == 1 ? " is" : "s are")
                + " still attached. Close "
                + (attached_clients == 1 ? "it" : "them")
                + " and retry.");
    }

    size_t checkpoints_removed = 0;
    for (const std::string& session_id : session_ids)
    {
        ControlMethodResult result = delete_session({
            { "session_id", session_id },
            { "confirm_live_terminals", true },
        });
        if (!result.ok)
            return result;
        if (result.value.value(
                "checkpoint_removed", false))
        {
            ++checkpoints_removed;
        }
    }
    return ControlMethodResult::success({
        { "deleted", true },
        { "deleted_sessions", session_ids.size() },
        { "stopped_live_terminals", live_terminals },
        { "checkpoints_removed", checkpoints_removed },
    });
}

ControlMethodResult ServerKernel::Impl::rename_session(
    const nlohmann::json& params)
{
    if (!params.is_object()
        || !params.contains("session_id")
        || !params["session_id"].is_string()
        || !params.contains("session_name")
        || !params["session_name"].is_string())
    {
        return ControlMethodResult::error(
            "invalid_session",
            "Session renaming requires string session_id and session_name values.");
    }
    const std::string session_id
        = params["session_id"].get<std::string>();
    const std::string session_name
        = params["session_name"].get<std::string>();
    if (!valid_server_session_id(session_id)
        || session_name.empty()
        || session_name.size() > kServerMaxSessionIdBytes
        || std::ranges::any_of(session_name,
            [](unsigned char ch) {
                return ch < 0x20 || ch == 0x7f;
            }))
    {
        return ControlMethodResult::error(
            "invalid_session",
            "The requested server Session identity or name is invalid.");
    }
    const auto found = sessions.find(session_id);
    if (found == sessions.end())
    {
        return ControlMethodResult::error(
            "session_not_found",
            "The requested server Session does not exist.");
    }
    if (found->second->checkpoint_task)
    {
        return ControlMethodResult::error(
            "checkpoint_busy",
            "The Session checkpoint is still being written. Retry shortly.");
    }
    found->second->session_name = session_name;
    std::string checkpoint_error;
    if (!checkpoint_session(session_id, checkpoint_error))
    {
        return ControlMethodResult::error(
            "checkpoint_failed",
            checkpoint_error.empty()
                ? "Unable to schedule the renamed Session checkpoint."
                : checkpoint_error);
    }
    return ControlMethodResult::success({
        { "renamed", true },
        { "session_id", session_id },
        { "session_name", session_name },
    });
}

bool ServerKernel::Impl::initialize_services(std::string& error)
{
    reset_services();
    fake_terminal = std::make_unique<FakeTerminalRuntime>();
    fake_terminal_service = std::make_unique<RemoteTerminalService>(
        RemoteTerminalServiceOptions{
            .method_prefix = "fake",
            .server_epoch = epoch_value,
            .pane_id = std::string(kFakeRemotePaneId),
            .terminal_id = std::string(kFakeRemoteTerminalId),
            .name = "Fake Remote",
        },
        *fake_terminal);
    std::vector<std::string> session_ids;
    session_ids.reserve(sessions.size());
    for (const auto& [session_id, session] : sessions)
    {
        (void)session;
        session_ids.push_back(session_id);
    }
    std::ranges::sort(session_ids);
    for (const auto& session_id : session_ids)
    {
        if (!initialize_session(session_id, error))
        {
            reset_services();
            return false;
        }
    }
    return true;
}


bool ServerKernel::Impl::checkpoint_session(
    std::string_view session_id, std::string& error)
{
    const auto found = sessions.find(std::string(session_id));
    if (found == sessions.end())
    {
        error = "The requested server Session does not exist.";
        return false;
    }
    ServerSession& server_session = *found->second;
    auto fail = [&](std::string message) {
        server_session.checkpoint_state = "failed";
        server_session.checkpoint_error = message.size() > 4096
            ? message.substr(0, 4096)
            : message;
        error = server_session.checkpoint_error;
        return false;
    };
    if (!server_session.topology_service)
        return fail("Server topology is unavailable.");
    if (server_session.checkpoint_task)
    {
        error = "The Session checkpoint is already being written.";
        return false;
    }
    if (server_session.corrupt_checkpoint_archive_required)
    {
        std::string archive_error;
        const auto archived = archive_corrupt_checkpoint(
            server_session.persistence_path, archive_error);
        if (!archived)
        {
            return fail(
                "Unable to archive the unreadable checkpoint: "
                + archive_error);
        }
        server_session.corrupt_checkpoint_archive_required
            = false;
        server_session.checkpoint_file_present = false;
        server_session.restore_warnings.push_back(
            "Archived the unreadable checkpoint as '"
            + archived->filename().string()
            + "'; checkpointing resumed.");
    }
    auto captured = capture_session_topology(
        server_session.topology_service->snapshot(), error);
    if (!captured)
        return fail(error);
    captured->session_name = server_session.session_name;
    const uint64_t revision
        = server_session.topology_service->snapshot().revision;
    auto task
        = std::make_shared<ServerSession::CheckpointTask>();
    task->revision = revision;
    server_session.checkpoint_task = task;
    server_session.checkpoint_state = "writing";
    server_session.checkpoint_error.clear();
    const auto destination = server_session.persistence_path;
    auto save = options.checkpoint_save;
    std::thread([task, captured = std::move(*captured),
                    destination, save = std::move(save)]() mutable {
        std::string save_error;
        bool saved = false;
        try
        {
            saved = save(
                captured, destination, &save_error);
        }
        catch (const std::exception& ex)
        {
            save_error
                = "Session checkpoint writer threw an exception: "
                + std::string(ex.what());
        }
        catch (...)
        {
            save_error
                = "Session checkpoint writer threw an unknown exception.";
        }
        {
            std::lock_guard guard(task->mutex);
            task->success = saved;
            task->error = saved
                ? std::string{}
                : (save_error.empty()
                          ? "Unable to save Session checkpoint."
                          : std::move(save_error));
            task->saved_unix_ms = current_unix_time_ms();
            task->finished = true;
        }
        task->ready.notify_all();
    }).detach();
    error.clear();
    return true;
}

void ServerKernel::Impl::collect_checkpoint_results()
{
    for (auto& [session_id, session] : sessions)
    {
        (void)session_id;
        const auto task = session->checkpoint_task;
        if (!task)
            continue;
        std::lock_guard guard(task->mutex);
        if (!task->finished)
            continue;
        if (task->success)
        {
            session->checkpoint_state = "ok";
            session->checkpoint_error.clear();
            session->last_checkpoint_unix_ms
                = task->saved_unix_ms;
            session->last_checkpoint_revision
                = task->revision;
            session->checkpoint_file_present = true;
        }
        else
        {
            session->checkpoint_state = "failed";
            session->checkpoint_error = task->error.size() > 4096
                ? task->error.substr(0, 4096)
                : task->error;
        }
        session->checkpoint_task.reset();
    }
}

void ServerKernel::Impl::wait_for_checkpoint_tasks(
    std::chrono::steady_clock::time_point deadline)
{
    for (auto& [session_id, session] : sessions)
    {
        (void)session_id;
        const auto task = session->checkpoint_task;
        if (!task)
            continue;
        std::unique_lock lock(task->mutex);
        task->ready.wait_until(
            lock, deadline, [&] { return task->finished; });
    }
    collect_checkpoint_results();
}

bool ServerKernel::Impl::read_session_id(
    const nlohmann::json& params, std::string& session_id,
    std::string& error) const
{
    session_id = "default";
    if (!params.is_object())
    {
        error = "Request parameters must be an object.";
        return false;
    }
    if (const auto value = params.find("session_id");
        value != params.end())
    {
        if (!value->is_string())
        {
            error = "Session identity must be a string.";
            return false;
        }
        session_id = value->get<std::string>();
    }
    if (!valid_server_session_id(session_id))
    {
        error = "Session identity is invalid.";
        return false;
    }
    return true;
}



ServerStatusSnapshot ServerKernel::Impl::status_snapshot() const
{
    size_t connected_clients = 0;
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard guard(mutex);
        connected_clients = static_cast<size_t>(
            std::ranges::count_if(
                clients, [this, now](const auto& item) {
                    return now - item.second.last_activity
                        <= options.client_activity_timeout;
                }));
    }
    const uint64_t uptime_ms = started
        ? static_cast<uint64_t>(
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - started_at)
                  .count())
        : 0;
    std::vector<ServerSessionStatusSnapshot> session_statuses;
    session_statuses.reserve(sessions.size());
    size_t space_count = 0;
    size_t terminal_count = 0;
    size_t agent_count = 0;
    for (const auto& [session_id, session] : sessions)
    {
        const size_t spaces = session->topology_service
            ? session->topology_service->snapshot().spaces.size()
            : (session->restored_topology
                      ? session->restored_topology->spaces.size()
                      : 0);
        const size_t live_terminals
            = static_cast<size_t>(std::ranges::count_if(
                session->terminals,
                [](const auto& item) {
                    return item.second.runtime->is_running();
                }));
        space_count += spaces;
        terminal_count += session->terminals.size();
        if (session->agent_service)
        {
            agent_count += session->agent_service
                               ->snapshot()
                               .agents.size();
        }
        session_statuses.push_back({
            .session_id = session_id,
            .session_name = session->session_name,
            .spaces = spaces,
            .terminals = session->terminals.size(),
            .live_terminals = live_terminals,
            .checkpoint_path = session->persistence_path.string(),
            .checkpoint_state = session->checkpoint_state,
            .last_checkpoint_unix_ms
            = session->last_checkpoint_unix_ms,
            .checkpoint_error = session->checkpoint_error,
            .restore_warnings = session->restore_warnings,
        });
    }
    std::ranges::sort(session_statuses,
        {}, &ServerSessionStatusSnapshot::session_id);

    std::string checkpoint_path;
    std::string checkpoint_state;
    uint64_t last_checkpoint_unix_ms = 0;
    std::string checkpoint_error;
    std::vector<std::string> restore_warnings
        = unassigned_restore_warnings;
    if (const auto default_session = sessions.find("default");
        default_session != sessions.end())
    {
        const ServerSession& session = *default_session->second;
        checkpoint_path = session.persistence_path.string();
        checkpoint_state = session.checkpoint_state;
        last_checkpoint_unix_ms = session.last_checkpoint_unix_ms;
        checkpoint_error = session.checkpoint_error;
        restore_warnings.insert(restore_warnings.end(),
            session.restore_warnings.begin(),
            session.restore_warnings.end());
    }

    return {
        .state = stop_requested ? "stopping" : (started ? "ready" : "stopped"),
        .protocol_major = options.protocol_major,
        .protocol_minor = options.protocol_minor,
        .server_pid = pid,
        .server_epoch = epoch_value,
        .build_version = options.build_version,
        .uptime_ms = uptime_ms,
        .connected_clients = connected_clients,
        .sessions = sessions.size(),
        .spaces = space_count,
        .terminals = terminal_count,
        .agents = agent_count,
        .scrollback_cells_reserved
        = terminal_resource_budget
            ? terminal_resource_budget
                  ->reserved_scrollback_cells()
            : 0,
        .scrollback_cells_limit
        = terminal_resource_budget
            ? terminal_resource_budget
                  ->max_scrollback_cells()
            : 0,
        .checkpoint_path = std::move(checkpoint_path),
        .checkpoint_state = std::move(checkpoint_state),
        .last_checkpoint_unix_ms
        = last_checkpoint_unix_ms,
        .checkpoint_error = std::move(checkpoint_error),
        .restore_warnings = std::move(restore_warnings),
        .session_statuses = std::move(session_statuses),
    };
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
