#include "server_kernel_impl.h"

#include <draxul/log.h>
#include <draxul/process_util.h>
#include <draxul/server_protocol.h>
#include <draxul/session_state.h>

#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>

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

constexpr size_t kFatalListenerFailureCount = 8;
constexpr auto kAgentRefreshInterval = std::chrono::seconds(1);

uint64_t current_process_id()
{
#ifdef _WIN32
    return static_cast<uint64_t>(GetCurrentProcessId());
#else
    return static_cast<uint64_t>(::getpid());
#endif
}

std::optional<std::string> process_start_token(uint64_t pid)
{
    if (pid == 0)
        return std::nullopt;
#ifdef _WIN32
    if (pid > std::numeric_limits<DWORD>::max())
        return std::nullopt;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE, static_cast<DWORD>(pid));
    if (!process)
        return std::nullopt;
    // Qualified: the shared HANDLE overload lives at draxul scope and would
    // otherwise be hidden by this anonymous-namespace uint64_t overload.
    const auto token = draxul::process_start_token(process);
    CloseHandle(process);
    return token;
#elif defined(__APPLE__)
    if (pid > static_cast<uint64_t>(
            std::numeric_limits<pid_t>::max()))
    {
        return std::nullopt;
    }
    kinfo_proc process{};
    size_t size = sizeof(process);
    int query[] = {
        CTL_KERN,
        KERN_PROC,
        KERN_PROC_PID,
        static_cast<int>(pid),
    };
    if (::sysctl(query, 4, &process, &size, nullptr, 0) != 0
        || size == 0)
    {
        return std::nullopt;
    }
    return std::to_string(process.kp_proc.p_starttime.tv_sec)
        + ":" + std::to_string(process.kp_proc.p_starttime.tv_usec);
#else
    std::ifstream stat(
        "/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!std::getline(stat, line))
        return std::nullopt;
    const size_t command_end = line.rfind(')');
    if (command_end == std::string::npos
        || command_end + 2 >= line.size())
    {
        return std::nullopt;
    }
    std::istringstream fields(line.substr(command_end + 2));
    std::string field;
    for (int index = 0; index < 20; ++index)
    {
        if (!(fields >> field))
            return std::nullopt;
    }
    std::ifstream boot_id("/proc/sys/kernel/random/boot_id");
    std::string boot;
    if (!std::getline(boot_id, boot) || boot.empty())
        return std::nullopt;
    return boot + ":" + field;
#endif
}

std::filesystem::path starting_marker_path(
    const std::filesystem::path& runtime_directory, uint64_t pid)
{
    return runtime_directory / ("server-starting-" + std::to_string(pid) + ".json");
}

// A detached server's stderr goes to /dev/null, so a startup failure that
// leaves no trace is indistinguishable from "no server exists" — the client
// then reports Absent and can only guess. The failure marker records WHY the
// last start died; ServerClient::probe reads it (same literal name there) and
// a successful start deletes it.
std::filesystem::path failure_marker_path(
    const std::filesystem::path& runtime_directory)
{
    return runtime_directory / "server-failed.json";
}

} // namespace

ServerKernel::Impl::Impl(ServerKernelOptions value)
    : options(std::move(value))
{
    if (options.protocol_major < 0)
        options.protocol_major = kServerProtocolMajor;
    if (options.protocol_minor < 0)
        options.protocol_minor = kServerProtocolMinor;
    if (options.client_activity_timeout.count() <= 0)
        options.client_activity_timeout = std::chrono::seconds(10);
    if (options.max_terminals == 0
        || options.max_terminals > kServerMaxTerminals)
    {
        options.max_terminals = kServerMaxTerminals;
    }
    if (options.max_scrollback_cells == 0
        || options.max_scrollback_cells > kServerMaxScrollbackCells)
    {
        options.max_scrollback_cells = kServerMaxScrollbackCells;
    }
    terminal_resource_budget
        = std::make_shared<ServerTerminalResourceBudget>(
            options.max_scrollback_cells);
    if (options.checkpoint_shutdown_budget.count() < 0)
        options.checkpoint_shutdown_budget = std::chrono::milliseconds(0);
    if (!options.checkpoint_save)
        options.checkpoint_save = save_session_state_to_path;
    if (options.build_version.empty())
        options.build_version = server_build_version();
    for (const AgentDefinition& definition : options.agent_definitions)
        agent_definitions.register_definition(definition);
    epoch_value = options.epoch_override.empty()
        ? random_epoch()
        : options.epoch_override;
    pid = current_process_id();
    process_start_identity = process_start_token(pid).value_or("");
}

void ServerKernel::Impl::publish_starting_marker()
{
    std::error_code directory_error;
    std::filesystem::create_directories(options.runtime_directory, directory_error);
#ifndef _WIN32
    if (!directory_error)
        ::chmod(options.runtime_directory.c_str(), 0700);
#endif
    starting_marker = starting_marker_path(options.runtime_directory, pid);
    std::ofstream output(starting_marker, std::ios::binary | std::ios::trunc);
    if (output)
    {
        output << nlohmann::json{
            { "pid", pid },
            { "epoch", epoch_value },
            { "process_start_token",
                process_start_identity },
            { "created_unix_ms",
                current_unix_time_ms() },
            { "protocol_major", options.protocol_major },
        }
                      .dump();
        output.flush();
    }
}

bool ServerKernel::Impl::published_identity_matches() const
{
    std::error_code size_error;
    const auto metadata_path = control.metadata_path();
    const auto size = std::filesystem::file_size(metadata_path, size_error);
    if (size_error || size == 0 || size > 16 * 1024)
        return false;
    std::ifstream input(metadata_path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    const auto metadata = nlohmann::json::parse(bytes, nullptr, false);
    if (metadata.is_discarded() || !metadata.is_object())
        return false;
    return metadata.contains("server_pid")
        && metadata["server_pid"].is_number_unsigned()
        && metadata["server_pid"].get<uint64_t>() == pid
        && metadata.contains("server_process_start_token")
        && metadata["server_process_start_token"].is_string()
        && metadata["server_process_start_token"]
               .get_ref<const std::string&>()
        == process_start_identity;
}

void ServerKernel::Impl::publish_failure_marker(std::string_view reason)
{
    std::error_code directory_error;
    std::filesystem::create_directories(
        options.runtime_directory, directory_error);
    std::ofstream output(failure_marker_path(options.runtime_directory),
        std::ios::binary | std::ios::trunc);
    if (!output)
        return;
    output << nlohmann::json{
        { "pid", pid },
        { "error", std::string(reason) },
        { "created_unix_ms", current_unix_time_ms() },
    }
                  .dump();
    output.flush();
}

void ServerKernel::Impl::remove_failure_marker()
{
    std::error_code ignored;
    std::filesystem::remove(
        failure_marker_path(options.runtime_directory), ignored);
}

void ServerKernel::Impl::remove_starting_marker()
{
    if (starting_marker.empty())
        return;
    std::error_code ignored;
    std::filesystem::remove(starting_marker, ignored);
    starting_marker.clear();
}

void ServerKernel::Impl::remove_all_starting_markers()
{
    std::error_code iteration_error;
    for (std::filesystem::directory_iterator it(
             options.runtime_directory, iteration_error);
        !iteration_error && it != std::filesystem::directory_iterator();
        it.increment(iteration_error))
    {
        const std::string name = it->path().filename().string();
        if (!name.starts_with("server-starting-")
            || it->path().extension() != ".json")
        {
            continue;
        }
        std::error_code remove_error;
        std::filesystem::remove(it->path(), remove_error);
    }
    starting_marker.clear();
}

ServerStartResult ServerKernel::Impl::start()
{
    if (started)
        return { ServerStartDisposition::Started, {} };
    if (options.runtime_directory.empty())
        return { ServerStartDisposition::Failed, "Server runtime directory is empty." };
    if (process_start_identity.empty())
    {
        return {
            ServerStartDisposition::Failed,
            "Unable to determine the Draxul server process start identity.",
        };
    }

    stop_requested = false;
    publish_starting_marker();
    std::string error;
    if (!prepare_session_restore(error))
    {
        remove_starting_marker();
        publish_failure_marker(error);
        return { ServerStartDisposition::Failed, std::move(error) };
    }
    const nlohmann::json metadata{
        { "server_pid", pid },
        { "server_epoch", epoch_value },
        { "server_process_start_token",
            process_start_identity },
        { "published_unix_ms", current_unix_time_ms() },
        { "server_protocol_major", options.protocol_major },
        { "server_protocol_minor", options.protocol_minor },
        { "build_version", options.build_version },
        { "client_executable", options.client_executable.string() },
        { "state", "ready" },
    };
    if (!control.start(
            namespaced_control_id(kServerControlId, options.runtime_directory),
            options.runtime_directory,
            [weak_loop_wake = std::weak_ptr(loop_wake)]() {
                if (const auto state = weak_loop_wake.lock())
                {
                    state->control_work_pending = true;
                    state->condition.notify_one();
                }
            },
            &error, metadata))
    {
        remove_starting_marker();
        if (control.endpoint_in_use())
            return { ServerStartDisposition::AlreadyRunning, std::move(error) };
        publish_failure_marker(error);
        return { ServerStartDisposition::Failed, std::move(error) };
    }
    session_stream = std::make_unique<SessionStreamService>(
        SessionStreamServiceOptions{
            .runtime_directory = options.runtime_directory,
            .server_epoch = epoch_value,
            .wake_state_thread = [weak_loop_wake
                                  = std::weak_ptr(loop_wake)]() {
                if (const auto state = weak_loop_wake.lock())
                {
                    state->control_work_pending = true;
                    state->condition.notify_one();
                }
            },
        });
    if (!session_stream->start(error))
    {
        session_stream.reset();
        control.stop();
        remove_starting_marker();
        publish_failure_marker(error);
        return { ServerStartDisposition::Failed, std::move(error) };
    }

    started_at = std::chrono::steady_clock::now();
    started = true;
    // Once this process owns the singleton endpoint, no other starting marker
    // can represent a second valid owner. Clean crash leftovers and losing
    // concurrent launchers together.
    remove_all_starting_markers();
    remove_failure_marker();
    DRAXUL_LOG_INFO(LogCategory::App,
        "Draxul server ready pid=%llu epoch=%s protocol=%d.%d",
        static_cast<unsigned long long>(pid),
        epoch_value.c_str(),
        options.protocol_major,
        options.protocol_minor);
    return { ServerStartDisposition::Started, {} };
}

int ServerKernel::Impl::run_until_stopped()
{
    if (!started)
        return 1;
    std::string initialization_error;
    if (!initialize_services(initialization_error))
    {
        DRAXUL_LOG_ERROR(LogCategory::App,
            "Draxul server could not initialize restored Session services: %s",
            initialization_error.c_str());
        stop();
        return 1;
    }
    size_t recent_listener_failures = 0;
    auto last_listener_failure
        = std::chrono::steady_clock::time_point{};
    bool fatal_listener_failure = false;
    auto next_eviction_check = std::chrono::steady_clock::now()
        + options.eviction_check_interval;
    int eviction_strikes = 0;
    while (!stop_requested)
    {
        loop_wake->terminal_output_pending = false;
        // Retire when this process is no longer the PUBLISHED server. Without
        // this, a server whose metadata was wiped or replaced ran forever —
        // invisible to clients, unreachable by the CLI, tray icon its only
        // surface — and every later launch added another one. Two consecutive
        // failed checks tolerate transient filesystem states (the metadata
        // write is not atomic).
        if (options.eviction_check_interval.count() > 0
            && std::chrono::steady_clock::now() >= next_eviction_check)
        {
            next_eviction_check = std::chrono::steady_clock::now()
                + options.eviction_check_interval;
            if (published_identity_matches())
            {
                eviction_strikes = 0;
            }
            else if (++eviction_strikes >= 2)
            {
                DRAXUL_LOG_INFO(LogCategory::App,
                    "Draxul server pid=%llu is no longer the published "
                    "endpoint owner; retiring gracefully",
                    static_cast<unsigned long long>(pid));
                // The path now belongs to a successor (or to nobody): this
                // instance's shutdown must not unlink the successor's socket
                // or metadata.
                control.abandon_endpoint();
                stop_requested = true;
                break;
            }
        }
        for (auto& [session_id, session] : sessions)
        {
            try
            {
                struct CleanExit
                {
                    std::string terminal_id;
                    uint64_t generation = 0;
                    uint64_t topology_revision = 0;
                };
                std::vector<CleanExit> clean_exits;
                const uint64_t topology_revision
                    = session->topology_service
                    ? session->topology_service
                          ->snapshot()
                          .revision
                    : 0;
                for (auto& [terminal_id, endpoint] : session->terminals)
                {
                    endpoint.service->pump();
                    if (endpoint.runtime->is_running())
                    {
                        endpoint.exit_cleanup_attempted = false;
                        continue;
                    }
                    if (!endpoint.service->started())
                        continue;
                    const std::optional<int> exit_code
                        = endpoint.runtime->exit_code();
                    if (exit_code && *exit_code != 0)
                        continue;
                    const uint64_t generation
                        = endpoint.service->generation();
                    if (endpoint.exit_cleanup_attempted
                        && endpoint.exit_cleanup_generation
                            == generation
                        && endpoint.exit_cleanup_topology_revision
                            == topology_revision)
                    {
                        continue;
                    }
                    clean_exits.push_back({
                        .terminal_id = terminal_id,
                        .generation = generation,
                        .topology_revision = topology_revision,
                    });
                }
                for (const CleanExit& exited : clean_exits)
                {
                    const auto closed
                        = session->topology_service
                        ? session->topology_service
                              ->close_exited_terminal(
                                  exited.terminal_id)
                        : ControlMethodResult::error(
                              "topology_unavailable",
                              "Session topology is unavailable.");
                    if (closed.ok)
                    {
                        refresh_agents(
                            *session,
                            std::chrono::steady_clock::now());
                        continue;
                    }
                    const auto retained
                        = session->terminals.find(
                            exited.terminal_id);
                    if (retained
                        != session->terminals.end())
                    {
                        retained->second
                            .exit_cleanup_attempted = true;
                        retained->second
                            .exit_cleanup_generation
                            = exited.generation;
                        retained->second
                            .exit_cleanup_topology_revision
                            = exited.topology_revision;
                    }
                }
            }
            catch (const std::exception& error)
            {
                DRAXUL_LOG_ERROR(LogCategory::App,
                    "Draxul server Session '%s' runtime pump failed; other Sessions remain available: %s",
                    session_id.c_str(), error.what());
            }
            catch (...)
            {
                DRAXUL_LOG_ERROR(LogCategory::App,
                    "Draxul server Session '%s' runtime pump failed with an unknown error; other Sessions remain available",
                    session_id.c_str());
            }
        }
        const uint32_t listener_error
            = options.listener_error_source
            ? options.listener_error_source()
            : control.take_listener_error();
        if (listener_error != 0)
        {
            const auto failure_time
                = std::chrono::steady_clock::now();
            if (last_listener_failure
                    != std::chrono::steady_clock::time_point{}
                && failure_time - last_listener_failure
                    > std::chrono::seconds(1))
            {
                recent_listener_failures = 0;
            }
            last_listener_failure = failure_time;
            ++recent_listener_failures;
            DRAXUL_LOG_WARN(LogCategory::App,
                "Draxul server control listener recreation failed with platform error %u (%zu/%zu)",
                listener_error,
                recent_listener_failures,
                kFatalListenerFailureCount);
            if (recent_listener_failures
                >= kFatalListenerFailureCount)
            {
                DRAXUL_LOG_ERROR(LogCategory::App,
                    "Draxul server control listener repeatedly failed; stopping the server so discovery can recover");
                fatal_listener_failure = true;
                request_stop();
                break;
            }
        }
        else if (last_listener_failure
                != std::chrono::steady_clock::time_point{}
            && std::chrono::steady_clock::now()
                    - last_listener_failure
                > std::chrono::seconds(1))
        {
            recent_listener_failures = 0;
        }
        collect_checkpoint_results();
        loop_wake->control_work_pending = false;
        control.process_pending(
            [this](const ControlRequest& request) {
                return handle_request(request);
            });
        if (session_stream)
        {
            session_stream->pump(
                [this](std::string_view session_id,
                    std::string_view client_id,
                    const SessionPollRequest& request,
                    size_t payload_budget) {
                    return build_session_poll(session_id, client_id, request,
                        payload_budget);
                },
                [this](std::string_view client_id) {
                    std::lock_guard guard(mutex);
                    const auto client = clients.find(std::string(client_id));
                    if (client != clients.end())
                    {
                        client->second.last_activity
                            = std::chrono::steady_clock::now();
                    }
                },
                [this](std::string_view session_id,
                    std::string_view client_id,
                    const SessionStreamCommand& command) {
                    return dispatch_stream_command(
                        session_id, client_id, command);
                });
        }
        const auto now = std::chrono::steady_clock::now();
        prune_inactive_clients(now);
        for (auto& [session_id, session] : sessions)
        {
            if (now >= session->next_agent_refresh_at)
            {
                try
                {
                    refresh_agents(*session, now);
                }
                catch (const std::exception& error)
                {
                    DRAXUL_LOG_ERROR(LogCategory::App,
                        "Draxul server Session '%s' agent refresh failed; other Sessions remain available: %s",
                        session_id.c_str(), error.what());
                }
                catch (...)
                {
                    DRAXUL_LOG_ERROR(LogCategory::App,
                        "Draxul server Session '%s' agent refresh failed with an unknown error; other Sessions remain available",
                        session_id.c_str());
                }
                session->next_agent_refresh_at
                    = now + kAgentRefreshInterval;
            }
        }
        if (options.session_checkpoint_interval.count() > 0)
        {
            for (auto& [session_id, session] : sessions)
            {
                if (now < session->next_checkpoint_at)
                {
                    continue;
                }
                if (!session->checkpoint_task
                    && session->topology_service
                    && (!session->checkpoint_file_present
                        || session->topology_service
                                ->snapshot()
                                .revision
                            != session->last_checkpoint_revision))
                {
                    std::string periodic_error;
                    if (!checkpoint_session(
                            session_id, periodic_error))
                    {
                        DRAXUL_LOG_WARN(LogCategory::App,
                            "Draxul server periodic Session '%s' checkpoint failed: %s",
                            session_id.c_str(),
                            periodic_error.c_str());
                    }
                }
                session->next_checkpoint_at
                    = now + options.session_checkpoint_interval;
            }
        }
        const auto idle_wait = recent_listener_failures > 0
            ? std::chrono::milliseconds(25)
            : options.idle_wait_interval;
        std::unique_lock lock(loop_wake->mutex);
        loop_wake->condition.wait_for(lock, idle_wait,
            [this] {
                return stop_requested.load()
                    || loop_wake->control_work_pending.load()
                    || loop_wake->terminal_output_pending.load();
            });
    }
    control.process_pending(
        [this](const ControlRequest& request) {
            return handle_request(request);
        });
    const auto checkpoint_deadline
        = std::chrono::steady_clock::now()
        + options.checkpoint_shutdown_budget;
    collect_checkpoint_results();
    for (const auto& [session_id, session] : sessions)
    {
        if (session->checkpoint_task)
            continue;
        std::string checkpoint_error;
        if (!checkpoint_session(session_id, checkpoint_error))
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "Draxul server Session '%s' checkpoint was not written: %s",
                session_id.c_str(),
                checkpoint_error.c_str());
        }
    }
    wait_for_checkpoint_tasks(checkpoint_deadline);
    // A periodic write may have captured revision N while a final request
    // advanced the topology to N+1. Once that write completes, use the
    // remaining shared shutdown budget for the truly final snapshot.
    if (std::chrono::steady_clock::now() < checkpoint_deadline)
    {
        for (const auto& [session_id, session] : sessions)
        {
            if (session->checkpoint_task
                || !session->topology_service
                || (session->checkpoint_file_present
                    && session->topology_service->snapshot().revision
                        == session->last_checkpoint_revision))
            {
                continue;
            }
            std::string checkpoint_error;
            if (!checkpoint_session(session_id, checkpoint_error))
            {
                DRAXUL_LOG_WARN(LogCategory::App,
                    "Draxul server Session '%s' final checkpoint was not scheduled: %s",
                    session_id.c_str(),
                    checkpoint_error.c_str());
            }
        }
        wait_for_checkpoint_tasks(checkpoint_deadline);
    }
    for (const auto& [session_id, session] : sessions)
    {
        if (session->checkpoint_task)
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "Draxul server Session '%s' checkpoint exceeded the shutdown budget; the previous completed checkpoint remains authoritative",
                session_id.c_str());
        }
    }
    reset_services();
    stop();
    return fatal_listener_failure ? 1 : 0;
}

void ServerKernel::Impl::request_stop()
{
    stop_requested = true;
    loop_wake->condition.notify_all();
}

void ServerKernel::Impl::stop()
{
    request_stop();
    if (session_stream)
    {
        session_stream->stop();
        session_stream.reset();
    }
    control.stop();
    remove_starting_marker();
    started = false;
}

} // namespace draxul
