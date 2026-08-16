#include <draxul/server_kernel.h>

#include "fake_terminal_runtime.h"
#include "remote_terminal_service.h"
#include "server_terminal_runtime.h"
#include "session_topology_bridge.h"
#include "topology_service.h"

#include <draxul/topology_layout.h>

#include <draxul/control_plane.h>
#include <draxul/log.h>
#include <draxul/remote_terminal_protocol.h>
#include <draxul/server_agent_service.h>
#include <draxul/server_protocol.h>
#include <draxul/session_state.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iomanip>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
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

constexpr size_t kFatalListenerFailureCount = 8;
constexpr size_t kCompletedAgentMutationLimit = 1024;
constexpr auto kAgentRefreshInterval
    = std::chrono::seconds(1);

struct ServerLoopWakeState
{
    std::mutex mutex;
    std::condition_variable condition;
    std::atomic<bool> control_work_pending = false;
    std::atomic<bool> terminal_output_pending = false;
};

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

uint64_t current_process_id()
{
#ifdef _WIN32
    return static_cast<uint64_t>(GetCurrentProcessId());
#else
    return static_cast<uint64_t>(::getpid());
#endif
}

uint64_t current_unix_time_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

#ifdef _WIN32
std::optional<std::string> process_start_token(HANDLE process)
{
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
        return std::nullopt;
    const uint64_t value
        = (static_cast<uint64_t>(created.dwHighDateTime) << 32)
        | created.dwLowDateTime;
    return std::to_string(value);
}
#endif

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
    const auto token = process_start_token(process);
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

std::string random_epoch()
{
    std::random_device random;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int index = 0; index < 16; ++index)
        out << std::setw(2) << (random() & 0xff);
    return out.str();
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

bool is_session_scoped_method(std::string_view method)
{
    return method.starts_with("terminal.")
        || method.starts_with("topology.")
        || method.starts_with("agent.")
        || method.starts_with("pane.");
}

const std::vector<std::string>& server_capabilities()
{
    static const std::vector<std::string> capabilities{
        "client-registration",
        "controller-lease",
        "agent-control-v1",
        "agent-projection-v1",
        std::string(kServerClientTokenCapability),
        "fake-remote-terminal",
        "graceful-shutdown",
        "managed-agent-v1",
        "managed-agent-v2",
        "multi-terminal-v1",
        "named-sessions-v1",
        "ordered-terminal-events",
        "real-remote-terminal",
        "session-delete-v1",
        "session-persistence-v1",
        "session-rename-v1",
        "status",
        "terminal-metrics-v1",
        "terminal-presentation-suspend-v1",
        "terminal-scrollback-v1",
        "terminal-uncompressed-v1",
        "topology-v1",
        "topology-control-v2",
        "client-plugin-pane-v1",
    };
    return capabilities;
}

std::vector<std::string> negotiate_capabilities(
    const std::vector<std::string>& requested)
{
    const std::unordered_set<std::string> supported(
        server_capabilities().begin(), server_capabilities().end());
    std::vector<std::string> result;
    for (const std::string& capability : requested)
    {
        if (supported.contains(capability))
            result.push_back(capability);
    }
    return result;
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

class ServerKernel::Impl
{
public:
    struct ServerSession;
    enum class ClientAccessResult
    {
        Accepted,
        LimitReached,
        InvalidToken,
    };

    struct ClientRegistration
    {
        std::chrono::steady_clock::time_point last_activity{};
        std::string connection_token;
        std::string registration_nonce;
        bool token_required = false;
    };

    explicit Impl(ServerKernelOptions value)
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
            || options.max_scrollback_cells
                > kServerMaxScrollbackCells)
        {
            options.max_scrollback_cells
                = kServerMaxScrollbackCells;
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
        {
            agent_definitions.register_definition(
                definition);
        }
        epoch_value = options.epoch_override.empty()
            ? random_epoch()
            : options.epoch_override;
        pid = current_process_id();
        process_start_identity
            = process_start_token(pid).value_or("");
    }

    ServerStartResult start();
    int run_until_stopped();
    void request_stop();
    void stop();
    ControlMethodResult handle_request(const ControlRequest& request);
    ServerStatusSnapshot status_snapshot() const;
    void publish_starting_marker();
    bool published_identity_matches() const;
    void publish_failure_marker(std::string_view reason);
    void remove_failure_marker();
    void remove_starting_marker();
    void remove_all_starting_markers();
    bool prepare_session_restore(std::string& error);
    bool initialize_services(std::string& error);
    bool initialize_session(
        std::string_view session_id, std::string& error);
    ServerSession* ensure_session(
        std::string_view session_id, std::string& error);
    enum class SessionServiceNeed
    {
        None,
        Topology,
        Agent,
        TopologyAndAgent,
    };
    // Shared preamble for session-scoped control methods: reads the
    // session id from params, ensures the session, and verifies that the
    // required services exist. On failure returns nullptr with `failure`
    // holding the error result; `unavailable_message` is used when the
    // session error text is empty.
    ServerSession* resolve_session(const nlohmann::json& params,
        SessionServiceNeed need, std::string_view unavailable_message,
        ControlMethodResult& failure);
    ControlMethodResult delete_session(
        const nlohmann::json& params);
    ControlMethodResult delete_all_sessions(
        const nlohmann::json& params);
    ControlMethodResult rename_session(
        const nlohmann::json& params);
    void reset_services();
    bool checkpoint_session(
        std::string_view session_id, std::string& error);
    void collect_checkpoint_results();
    void wait_for_checkpoint_tasks(
        std::chrono::steady_clock::time_point deadline);
    bool read_session_id(const nlohmann::json& params,
        std::string& session_id, std::string& error) const;
    std::optional<std::string> create_server_terminal(
        std::string_view session_id,
        const ServerTerminalTopologyLaunch& launch,
        std::string& error);
    ServerTerminalRuntimeOptions server_terminal_runtime_options(
        std::string_view session_id,
        std::string_view space_id,
        std::string_view tab_id,
        std::string_view pane_id,
        std::string_view terminal_id,
        std::string_view working_directory) const;
    bool create_server_terminal_with_id(std::string_view session_id,
        std::string terminal_id,
        std::string_view pane_id, std::string_view name,
        std::string& error,
        std::optional<ServerTerminalRuntimeOptions>
            runtime_options = std::nullopt,
        bool start_immediately = false,
        std::string preferred_controller_client_id = {});
    size_t server_terminal_count() const;
    std::optional<std::string>
    create_managed_agent_terminal(
        std::string_view session_id,
        std::string_view space_id,
        std::string_view tab_id,
        std::string_view pane_id,
        std::string_view name,
        const ManagedAgentTopologyLaunch& launch,
        std::string& error);
    std::optional<ServerTerminalRuntimeOptions>
    managed_agent_runtime_options(
        std::string_view session_id,
        std::string_view space_id,
        std::string_view tab_id,
        std::string_view pane_id,
        std::string_view terminal_id,
        const ManagedAgentTopologyLaunch& launch,
        std::string& error) const;
    void destroy_server_terminal(std::string_view session_id,
        std::string_view terminal_id);
    bool restart_server_terminal(std::string_view session_id,
        std::string_view terminal_id, std::string& error);
    void refresh_agents(ServerSession& session,
        std::chrono::steady_clock::time_point now);
    ClientAccessResult register_client_hello(
        const ServerHello& hello, bool token_capable,
        std::string& connection_token);
    ClientAccessResult authenticate_or_touch_client(
        std::string_view client_id,
        std::string_view connection_token);
    void disconnect_client(std::string_view client_id);
    void detach_client_from_services(std::string_view client_id);
    void remember_client_session(
        std::string_view client_id,
        std::string_view session_id);
    size_t active_clients_for_session(
        std::string_view session_id);
    void forget_session_clients(
        std::string_view session_id);
    void prune_inactive_clients(
        std::chrono::steady_clock::time_point now);

    ServerKernelOptions options;
    std::shared_ptr<ServerTerminalResourceBudget>
        terminal_resource_budget;
    AgentDefinitionRegistry agent_definitions;
    ControlServer control;
    std::string epoch_value;
    uint64_t pid = 0;
    std::string process_start_identity;
    std::chrono::steady_clock::time_point started_at{};
    std::atomic<bool> started = false;
    std::atomic<bool> stop_requested = false;
    mutable std::mutex mutex;
    std::shared_ptr<ServerLoopWakeState> loop_wake
        = std::make_shared<ServerLoopWakeState>();
    std::unordered_map<std::string, ClientRegistration> clients;
    std::unordered_map<std::string,
        std::unordered_set<std::string>>
        client_sessions;
    std::filesystem::path starting_marker;

    std::unique_ptr<FakeTerminalRuntime> fake_terminal;
    std::unique_ptr<RemoteTerminalService> fake_terminal_service;
    struct ServerTerminalEndpoint
    {
        std::unique_ptr<ServerTerminalRuntime> runtime;
        std::unique_ptr<RemoteTerminalService> service;
        bool exit_cleanup_attempted = false;
        uint64_t exit_cleanup_generation = 0;
        uint64_t exit_cleanup_topology_revision = 0;
    };
    struct ServerSession
    {
        struct CheckpointTask
        {
            std::mutex mutex;
            std::condition_variable ready;
            bool finished = false;
            bool success = false;
            std::string error;
            uint64_t revision = 0;
            uint64_t saved_unix_ms = 0;
        };
        std::string session_id;
        std::string session_name;
        std::unique_ptr<TopologyService> topology_service;
        std::unique_ptr<ServerAgentService> agent_service;
        std::unordered_map<std::string, ServerTerminalEndpoint>
            terminals;
        uint64_t next_terminal_serial = 2;
        uint64_t next_agent_serial = 1;
        std::filesystem::path persistence_path;
        std::vector<std::string> restore_warnings;
        std::optional<TopologySnapshot> restored_topology;
        std::string checkpoint_state = "pending";
        std::string checkpoint_error;
        uint64_t last_checkpoint_unix_ms = 0;
        uint64_t last_checkpoint_revision = 0;
        bool checkpoint_file_present = false;
        bool corrupt_checkpoint_archive_required = false;
        std::shared_ptr<CheckpointTask> checkpoint_task;
        std::chrono::steady_clock::time_point next_checkpoint_at{};
        std::chrono::steady_clock::time_point next_agent_refresh_at{};
        std::unordered_map<std::string, ControlMethodResult>
            completed_agent_mutations;
        std::deque<std::string> completed_agent_mutation_order;
    };
    std::unordered_map<std::string, std::unique_ptr<ServerSession>>
        sessions;
    std::vector<std::string> unassigned_restore_warnings;
};

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

ServerKernel::Impl::ClientAccessResult
ServerKernel::Impl::register_client_hello(
    const ServerHello& hello, bool token_capable,
    std::string& connection_token)
{
    const auto now = std::chrono::steady_clock::now();
    prune_inactive_clients(now);
    std::lock_guard guard(mutex);
    const auto found = clients.find(hello.client_id);
    if (found == clients.end()
        && clients.size() >= kServerMaxConnectedClients)
    {
        return ClientAccessResult::LimitReached;
    }
    if (found == clients.end())
    {
        if (token_capable && hello.registration_nonce.empty())
            return ClientAccessResult::InvalidToken;
        ClientRegistration registration{
            .last_activity = now,
            .registration_nonce = hello.registration_nonce,
            .token_required = token_capable,
        };
        if (token_capable)
            registration.connection_token = random_epoch();
        connection_token = registration.connection_token;
        clients.emplace(hello.client_id, std::move(registration));
        return ClientAccessResult::Accepted;
    }

    ClientRegistration& registration = found->second;
    if (registration.token_required
        && hello.connection_token != registration.connection_token)
    {
        if (hello.registration_nonce.empty()
            || hello.registration_nonce
                != registration.registration_nonce)
        {
            return ClientAccessResult::InvalidToken;
        }
    }
    if (!registration.token_required && token_capable)
    {
        if (hello.registration_nonce.empty())
            return ClientAccessResult::InvalidToken;
        registration.token_required = true;
        registration.connection_token = random_epoch();
        registration.registration_nonce
            = hello.registration_nonce;
    }
    else if (registration.token_required
        && hello.connection_token == registration.connection_token
        && !hello.registration_nonce.empty())
    {
        registration.registration_nonce
            = hello.registration_nonce;
    }
    registration.last_activity = now;
    connection_token = registration.connection_token;
    return ClientAccessResult::Accepted;
}

ServerKernel::Impl::ClientAccessResult
ServerKernel::Impl::authenticate_or_touch_client(
    std::string_view client_id,
    std::string_view connection_token)
{
    const auto now = std::chrono::steady_clock::now();
    prune_inactive_clients(now);
    std::lock_guard guard(mutex);
    const auto found = clients.find(std::string(client_id));
    if (found == clients.end())
    {
        if (!connection_token.empty())
            return ClientAccessResult::InvalidToken;
        if (clients.size() >= kServerMaxConnectedClients)
            return ClientAccessResult::LimitReached;
        clients.emplace(std::string(client_id),
            ClientRegistration{
                .last_activity = now,
            });
        return ClientAccessResult::Accepted;
    }

    ClientRegistration& registration = found->second;
    if (registration.token_required
        && connection_token != registration.connection_token)
    {
        return ClientAccessResult::InvalidToken;
    }
    if (!registration.token_required && !connection_token.empty())
        return ClientAccessResult::InvalidToken;
    registration.last_activity = now;
    return ClientAccessResult::Accepted;
}

void ServerKernel::Impl::detach_client_from_services(
    std::string_view client_id)
{
    if (fake_terminal_service)
        fake_terminal_service->disconnect_client(client_id);
    for (auto& [session_id, session] : sessions)
    {
        (void)session_id;
        for (auto& [terminal_id, endpoint] : session->terminals)
        {
            (void)terminal_id;
            endpoint.service->disconnect_client(client_id);
        }
    }
}

void ServerKernel::Impl::remember_client_session(
    std::string_view client_id,
    std::string_view session_id)
{
    std::lock_guard guard(mutex);
    if (clients.contains(std::string(client_id)))
    {
        client_sessions[std::string(client_id)]
            .insert(std::string(session_id));
    }
}

size_t ServerKernel::Impl::active_clients_for_session(
    std::string_view session_id)
{
    prune_inactive_clients(
        std::chrono::steady_clock::now());
    std::lock_guard guard(mutex);
    return static_cast<size_t>(
        std::ranges::count_if(
            client_sessions,
            [&](const auto& item) {
                return clients.contains(item.first)
                    && item.second.contains(
                        std::string(session_id));
            }));
}

void ServerKernel::Impl::forget_session_clients(
    std::string_view session_id)
{
    std::lock_guard guard(mutex);
    for (auto it = client_sessions.begin();
        it != client_sessions.end();)
    {
        it->second.erase(std::string(session_id));
        if (it->second.empty())
            it = client_sessions.erase(it);
        else
            ++it;
    }
}

void ServerKernel::Impl::disconnect_client(
    std::string_view client_id)
{
    {
        std::lock_guard guard(mutex);
        clients.erase(std::string(client_id));
        client_sessions.erase(std::string(client_id));
    }
    detach_client_from_services(client_id);
}

void ServerKernel::Impl::prune_inactive_clients(
    std::chrono::steady_clock::time_point now)
{
    std::vector<std::string> expired;
    {
        std::lock_guard guard(mutex);
        for (auto it = clients.begin(); it != clients.end();)
        {
            if (now - it->second.last_activity
                > options.client_activity_timeout)
            {
                expired.push_back(it->first);
                client_sessions.erase(it->first);
                it = clients.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    for (const auto& client_id : expired)
        detach_client_from_services(client_id);
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

ControlMethodResult ServerKernel::Impl::handle_request(
    const ControlRequest& request)
{
    if (request.method == "server.hello")
    {
        std::string parse_error;
        auto hello = server_hello_from_json(request.params, parse_error);
        if (!hello)
            return ControlMethodResult::error("invalid_hello", std::move(parse_error));
        if (hello->protocol_major != options.protocol_major)
        {
            return ControlMethodResult::error("incompatible_protocol",
                "Client/server protocol major versions do not match.");
        }

        const bool token_capable = std::ranges::find(
                                       hello->capabilities, kServerClientTokenCapability)
            != hello->capabilities.end();
        std::string connection_token;
        const ClientAccessResult registration
            = register_client_hello(
                *hello, token_capable, connection_token);
        if (registration == ClientAccessResult::LimitReached)
        {
            return ControlMethodResult::error(
                "client_limit_reached",
                "The Draxul server client limit has been reached.");
        }
        if (registration == ClientAccessResult::InvalidToken)
        {
            return ControlMethodResult::error(
                "invalid_connection_token",
                "The client identity is already bound to another connection.");
        }
        ServerWelcome welcome{
            .protocol_major = options.protocol_major,
            .protocol_minor = std::min(
                options.protocol_minor, hello->protocol_minor),
            .server_pid = pid,
            .server_epoch = epoch_value,
            .build_version = options.build_version,
            .connection_token = std::move(connection_token),
            .capabilities = negotiate_capabilities(hello->capabilities),
        };
        return ControlMethodResult::success(server_welcome_to_json(welcome));
    }
    std::string request_client_id;
    if (request.params.is_object())
    {
        const auto client_id = request.params.find("client_id");
        if (client_id != request.params.end())
        {
            if (!client_id->is_string()
                || !valid_server_client_id(
                    client_id->get_ref<const std::string&>()))
            {
                return ControlMethodResult::error(
                    "invalid_client",
                    "A valid client_id is required.");
            }
            request_client_id
                = client_id->get<std::string>();
            std::string connection_token;
            if (const auto token
                = request.params.find("connection_token");
                token != request.params.end())
            {
                if (!token->is_string()
                    || token->get_ref<const std::string&>().size()
                        > kServerMaxConnectionTokenBytes)
                {
                    return ControlMethodResult::error(
                        "invalid_connection_token",
                        "A valid connection token is required.");
                }
                connection_token = token->get<std::string>();
            }
            const ClientAccessResult access
                = authenticate_or_touch_client(
                    request_client_id, connection_token);
            if (access == ClientAccessResult::LimitReached)
            {
                return ControlMethodResult::error(
                    "client_limit_reached",
                    "The Draxul server client limit has been reached.");
            }
            if (access == ClientAccessResult::InvalidToken)
            {
                return ControlMethodResult::error(
                    "invalid_connection_token",
                    "The connection token does not match this client identity.");
            }
        }
    }
    if (request.method == "server.goodbye")
    {
        if (request_client_id.empty())
        {
            return ControlMethodResult::error(
                "invalid_client",
                "A valid client_id is required.");
        }
        disconnect_client(request_client_id);
        return ControlMethodResult::success({
            { "disconnected", true },
        });
    }
    if (!request_client_id.empty()
        && is_session_scoped_method(request.method))
    {
        std::string session_id;
        std::string session_error;
        if (read_session_id(
                request.params, session_id, session_error))
        {
            remember_client_session(
                request_client_id, session_id);
        }
    }
    if (request.method == "server.status")
        return ControlMethodResult::success(server_status_to_json(status_snapshot()));
    if (request.method == "server.delete_session")
        return delete_session(request.params);
    if (request.method == "server.delete_all_sessions")
        return delete_all_sessions(request.params);
    if (request.method == "server.rename_session")
        return rename_session(request.params);
    if (request.method.starts_with("fake."))
    {
        if (!fake_terminal_service)
        {
            return ControlMethodResult::error(
                "terminal_unavailable",
                "The fake remote terminal is unavailable.");
        }
        return fake_terminal_service->handle(
            request.method, request.params);
    }
    if (request.method.starts_with("terminal."))
    {
        ControlMethodResult failure;
        ServerSession* session = resolve_session(request.params,
            SessionServiceNeed::None, {}, failure);
        if (!session)
            return failure;
        std::string terminal_id
            = std::string(kServerShellTerminalId);
        if (request.params.is_object()
            && request.params.contains("terminal_id")
            && request.params["terminal_id"].is_string())
        {
            terminal_id
                = request.params["terminal_id"].get<std::string>();
        }
        const auto terminal = session->terminals.find(terminal_id);
        if (terminal == session->terminals.end())
        {
            return ControlMethodResult::error(
                "terminal_not_found",
                "The requested server terminal does not exist.");
        }
        return terminal->second.service->handle(
            request.method, request.params);
    }
    if (request.method == "topology.snapshot"
        || request.method == "topology.poll"
        || request.method == "topology.command"
        || request.method == "topology.layout_apply")
    {
        ControlMethodResult failure;
        ServerSession* session = resolve_session(request.params,
            SessionServiceNeed::Topology,
            "Server Session topology is unavailable.", failure);
        if (!session)
            return failure;
        return session->topology_service->handle(
            request.method, request.params);
    }
    if (request.method == "agent.snapshot"
        || request.method == "agent.poll"
        || request.method == "agent.list"
        || request.method == "agent.get"
        || request.method == "agent.explain"
        || request.method == "agent.wait"
        || request.method == "agent.start"
        || request.method == "agent.restart"
        || request.method == "agent.send_text"
        || request.method == "agent.send_keys")
    {
        ControlMethodResult failure;
        ServerSession* session = resolve_session(request.params,
            SessionServiceNeed::Agent,
            "Server Session agents are unavailable.", failure);
        if (!session)
            return failure;
        const bool mutating_agent_request
            = request.method == "agent.start"
            || request.method == "agent.restart"
            || request.method == "agent.send_text"
            || request.method == "agent.send_keys";
        std::string agent_mutation_key;
        if (mutating_agent_request
            && request.params.contains("request_id"))
        {
            const auto& request_id
                = request.params["request_id"];
            if (request_id.is_number_unsigned())
            {
                const uint64_t value
                    = request_id.get<uint64_t>();
                if (value == 0)
                {
                    return ControlMethodResult::error(
                        "invalid_request_id",
                        "Agent request_id must be non-zero.");
                }
                agent_mutation_key
                    = request.method + ":"
                    + std::to_string(value);
            }
            else if (request_id.is_string())
            {
                const auto& value
                    = request_id.get_ref<const std::string&>();
                if (value.empty() || value.size() > 256)
                {
                    return ControlMethodResult::error(
                        "invalid_request_id",
                        "Agent request_id must be a non-empty bounded string.");
                }
                agent_mutation_key
                    = request.method + ":" + value;
            }
            else
            {
                return ControlMethodResult::error(
                    "invalid_request_id",
                    "Agent request_id must be an unsigned integer or string.");
            }
            const auto cached
                = session->completed_agent_mutations.find(
                    agent_mutation_key);
            if (cached
                != session->completed_agent_mutations.end())
            {
                return cached->second;
            }
        }
        const auto remember_agent_mutation
            = [&](ControlMethodResult result) {
                  if (agent_mutation_key.empty()
                      || !result.ok)
                  {
                      return result;
                  }
                  session->completed_agent_mutation_order
                      .push_back(agent_mutation_key);
                  session->completed_agent_mutations[agent_mutation_key]
                      = result;
                  while (session
                             ->completed_agent_mutation_order
                             .size()
                      > kCompletedAgentMutationLimit)
                  {
                      session->completed_agent_mutations
                          .erase(session
                                  ->completed_agent_mutation_order
                                  .front());
                      session->completed_agent_mutation_order
                          .pop_front();
                  }
                  return result;
              };
        if (request.method == "agent.start")
        {
            if (!request.params.is_object()
                || !request.params.contains("profile_id")
                || !request.params["profile_id"].is_string())
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    "agent.start requires a string 'profile_id'.");
            }
            const std::string profile_id
                = request.params["profile_id"]
                      .get<std::string>();
            const AgentDefinition* definition
                = agent_definitions.find(profile_id);
            if (!definition)
            {
                return ControlMethodResult::error(
                    "unknown_profile",
                    "Managed agent profile is unavailable in the server.");
            }

            ManagedAgentTopologyLaunch launch{
                .restore_policy
                = definition->restore_policy,
            };
            if (request.params.contains("replace_pane"))
            {
                if (!request.params["replace_pane"].is_boolean())
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "'replace_pane' must be a boolean.");
                }
                launch.replace_target_pane
                    = request.params["replace_pane"].get<bool>();
            }
            if (request.params.contains("client_id"))
            {
                if (!request.params["client_id"].is_string()
                    || request.params["client_id"]
                        .get_ref<const std::string&>()
                        .empty()
                    || request.params["client_id"]
                            .get_ref<const std::string&>()
                            .size()
                        > 128)
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "'client_id' must be a non-empty bounded string.");
                }
                launch.preferred_controller_client_id
                    = request.params["client_id"]
                          .get<std::string>();
            }
            if (request.params.contains("args"))
            {
                if (!request.params["args"].is_array()
                    || request.params["args"].size() > 64)
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "'args' must be an array of at most 64 strings.");
                }
                if (!request.params["args"].empty())
                {
                    return ControlMethodResult::error(
                        "unsupported",
                        "Additional arguments for remote managed agents "
                        "are not durable yet; put stable arguments in "
                        "the agent profile.");
                }
                for (const auto& value : request.params["args"])
                {
                    if (!value.is_string()
                        || value
                                .get_ref<const std::string&>()
                                .size()
                            > 4096)
                    {
                        return ControlMethodResult::error(
                            "invalid_params",
                            "Every agent argument must be a bounded string.");
                    }
                    launch.additional_args.push_back(
                        value.get<std::string>());
                }
            }
            if (request.params.contains("cwd"))
            {
                if (!request.params["cwd"].is_string()
                    || request.params["cwd"]
                            .get_ref<const std::string&>()
                            .size()
                        > kTopologyMaxTextBytes)
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "'cwd' must be a bounded string.");
                }
                launch.working_directory
                    = request.params["cwd"]
                          .get<std::string>();
            }

            const TopologySnapshot& topology
                = session->topology_service->snapshot();
            if (topology.spaces.empty())
            {
                return ControlMethodResult::error(
                    "topology_unavailable",
                    "Server Session has no Space.");
            }
            const auto read_route_id
                = [&](const char* name,
                      std::string_view prefix)
                -> std::optional<std::string> {
                if (!request.params.contains(name))
                    return std::nullopt;
                const auto& value
                    = request.params[name];
                if (value.is_string()
                    && !value
                        .get_ref<const std::string&>()
                        .empty())
                {
                    return value.get<std::string>();
                }
                if (value.is_number_integer()
                    && value.get<int64_t>() >= 0)
                {
                    return std::string(prefix)
                        + std::to_string(
                            value.get<int64_t>());
                }
                return std::string{};
            };
            auto space_id = read_route_id(
                "space_id", "space-");
            if (space_id && space_id->empty())
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    "'space_id' must be a route id.");
            }
            if (!space_id)
                space_id = topology.spaces.front().space_id;
            const auto space = std::ranges::find(
                topology.spaces, *space_id,
                &TopologySpace::space_id);
            if (space == topology.spaces.end()
                || space->tabs.empty())
            {
                return ControlMethodResult::error(
                    "space_not_found",
                    "Topology Space was not found.");
            }

            auto tab_id = read_route_id(
                "tab_id", "tab-");
            if (tab_id && tab_id->empty())
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    "'tab_id' must be a route id.");
            }
            if (!tab_id)
                tab_id = space->tabs.front().tab_id;
            const auto tab = std::ranges::find(
                space->tabs, *tab_id,
                &TopologyTab::tab_id);
            if (tab == space->tabs.end()
                || tab->panes.empty())
            {
                return ControlMethodResult::error(
                    "tab_not_found",
                    "Topology tab was not found.");
            }

            auto pane_id = read_route_id(
                "pane_id", "pane-");
            if (pane_id && pane_id->empty())
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    "'pane_id' must be a route id.");
            }
            if (!pane_id)
                pane_id = tab->panes.front().pane_id;

            std::string instance_id;
            for (;;)
            {
                instance_id = "server-agent-"
                    + session->session_id + "-"
                    + std::to_string(
                        session->next_agent_serial++);
                bool used = false;
                for (const auto& candidate_space : topology.spaces)
                {
                    for (const auto& candidate_tab : candidate_space.tabs)
                    {
                        used = used
                            || std::ranges::any_of(
                                candidate_tab.panes,
                                [&](const TopologyPane& pane) {
                                    return pane.agent
                                        && pane.agent
                                               ->instance_id
                                        == instance_id;
                                });
                    }
                }
                if (!used)
                    break;
            }
            launch.identity = {
                .profile_id = definition->profile_id,
                .kind = definition->kind,
                .display_name = definition->display_name,
                .instance_id = instance_id,
                .origin = AgentIdentityOrigin::Managed,
            };
            if (launch.working_directory.empty())
            {
                launch.working_directory
                    = space->root_directory;
            }
            auto started
                = session->topology_service->launch_agent(
                    *space_id, *tab_id, *pane_id,
                    definition->display_name, launch);
            if (!started.ok)
                return started;
            refresh_agents(
                *session,
                std::chrono::steady_clock::now());
            return remember_agent_mutation(
                session->agent_service->handle(
                    "agent.get",
                    { { "instance_id", instance_id } }));
        }
        if (request.method == "agent.restart"
            || request.method == "agent.send_text"
            || request.method == "agent.send_keys")
        {
            if (!request.params.is_object()
                || !request.params.contains("instance_id")
                || !request.params["instance_id"].is_string())
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    request.method
                        + " requires 'instance_id'.");
            }
            const std::string instance_id
                = request.params["instance_id"]
                      .get<std::string>();
            const auto& agents
                = session->agent_service->snapshot().agents;
            const auto agent = std::ranges::find_if(
                agents,
                [&instance_id](
                    const ServerAgentProjection& value) {
                    return value.identity.instance_id
                        == instance_id;
                });
            if (agent == agents.end())
            {
                return ControlMethodResult::error(
                    "not_found", "Agent not found.");
            }
            const std::string terminal_id
                = agent->terminal_id;
            const auto terminal
                = session->terminals.find(terminal_id);
            if (terminal == session->terminals.end())
            {
                return ControlMethodResult::error(
                    "agent_replaced",
                    "The agent terminal no longer exists.");
            }
            if (request.method == "agent.restart")
            {
                std::string restart_error;
                if (!terminal->second.service
                        ->restart_runtime(restart_error))
                {
                    return ControlMethodResult::error(
                        "restart_failed",
                        std::move(restart_error));
                }
                refresh_agents(*session,
                    std::chrono::steady_clock::now());
                return remember_agent_mutation(
                    ControlMethodResult::success({
                        { "accepted", true },
                        { "terminal_id", terminal_id },
                        { "runtime_generation",
                            terminal->second.service
                                ->generation() },
                    }));
            }

            std::string bytes;
            if (request.method == "agent.send_text")
            {
                if (!request.params.contains("text")
                    || !request.params["text"].is_string())
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "agent.send_text requires string 'text'.");
                }
                bytes = request.params["text"].get<std::string>();
                if (bytes.size() > 64 * 1024)
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "Agent text exceeds 64 KiB.");
                }
            }
            else
            {
                if (!request.params.contains("keys")
                    || !request.params["keys"].is_array()
                    || request.params["keys"].size() > 64)
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "agent.send_keys requires at most 64 keys.");
                }
                std::vector<std::string> keys;
                keys.reserve(
                    request.params["keys"].size());
                for (const auto& value : request.params["keys"])
                {
                    if (!value.is_string())
                    {
                        return ControlMethodResult::error(
                            "invalid_params",
                            "Every key must be a string.");
                    }
                    keys.push_back(value.get<std::string>());
                }
                std::string key_error;
                auto encoded
                    = encode_agent_keys(keys, key_error);
                if (!encoded)
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        std::move(key_error));
                }
                bytes = std::move(*encoded);
            }
            const RemoteTerminalInputResult input_result
                = terminal->second.runtime->send_input(bytes);
            if (input_result
                != RemoteTerminalInputResult::Accepted)
            {
                return ControlMethodResult::error(
                    input_result
                            == RemoteTerminalInputResult::Backpressure
                        ? "backpressure"
                        : "input_failed",
                    input_result
                            == RemoteTerminalInputResult::Backpressure
                        ? "The agent terminal input queue is full."
                        : "The agent terminal rejected input.");
            }
            return remember_agent_mutation(
                session->agent_service->handle(
                    "agent.get",
                    { { "instance_id", instance_id } }));
        }
        return session->agent_service->handle(
            request.method, request.params);
    }
    if (request.method == "pane.report_agent_session")
    {
        ControlMethodResult failure;
        ServerSession* session = resolve_session(request.params,
            SessionServiceNeed::TopologyAndAgent,
            "Server Session is unavailable.", failure);
        if (!session)
            return failure;
        const auto required_string
            = [&](const char* name)
            -> std::optional<std::string> {
            if (!request.params.is_object()
                || !request.params.contains(name)
                || !request.params[name].is_string()
                || request.params[name]
                    .get_ref<const std::string&>()
                    .empty())
            {
                return std::nullopt;
            }
            return request.params[name].get<std::string>();
        };
        const auto read_unsigned
            = [&](const char* name)
            -> std::optional<uint64_t> {
            if (!request.params.contains(name))
                return std::nullopt;
            const auto& value = request.params[name];
            if (value.is_number_unsigned())
                return value.get<uint64_t>();
            if (value.is_number_integer()
                && value.get<int64_t>() >= 0)
            {
                return static_cast<uint64_t>(
                    value.get<int64_t>());
            }
            return std::nullopt;
        };
        const auto server_epoch
            = required_string("server_epoch");
        const auto pane_id = required_string("pane_id");
        const auto instance_id
            = required_string("agent_instance_id");
        const auto source = required_string("source");
        const auto agent_kind = required_string("agent");
        const auto ref_kind_text
            = required_string("ref_kind");
        const auto ref_value = required_string("ref_value");
        const auto integration_version
            = read_unsigned("integration_version");
        const auto sequence = read_unsigned("sequence");
        const auto runtime_generation
            = read_unsigned("runtime_generation");
        if (!server_epoch || !pane_id || !instance_id
            || !source || !agent_kind || !ref_kind_text
            || !ref_value || !integration_version
            || *integration_version == 0
            || *integration_version > UINT32_MAX
            || !sequence || *sequence == 0
            || !runtime_generation
            || *runtime_generation == 0)
        {
            return ControlMethodResult::error(
                "invalid_params",
                "pane.report_agent_session requires server epoch, "
                "runtime generation, complete routing, source, "
                "version, sequence, and reference fields.");
        }
        if (*server_epoch != epoch_value)
        {
            return ControlMethodResult::error(
                "server_replaced",
                "The session report targets an old server epoch.");
        }
        const auto ref_kind
            = parse_agent_session_ref_kind(*ref_kind_text);
        if (!ref_kind)
        {
            return ControlMethodResult::error(
                "invalid_params",
                "Unknown native session reference kind.");
        }

        refresh_agents(
            *session, std::chrono::steady_clock::now());
        const auto& agents
            = session->agent_service->snapshot().agents;
        const auto agent = std::ranges::find_if(
            agents,
            [&](const ServerAgentProjection& value) {
                return value.identity.instance_id
                    == *instance_id;
            });
        if (agent == agents.end()
            || agent->pane_id != *pane_id
            || agent->identity.kind != *agent_kind)
        {
            return ControlMethodResult::error(
                "routing_mismatch",
                "Agent routing identity does not match the pane.");
        }
        if (agent->generation.value
            != *runtime_generation)
        {
            return ControlMethodResult::error(
                "agent_replaced",
                "The session report targets an old agent runtime generation.");
        }

        AgentSessionRef session_ref{
            .source = *source,
            .agent_kind = *agent_kind,
            .integration_version
            = static_cast<uint32_t>(*integration_version),
            .sequence = *sequence,
            .kind = *ref_kind,
            .value = *ref_value,
        };
        auto reported
            = session->topology_service
                  ->report_agent_session(
                      *pane_id, *instance_id,
                      session_ref);
        if (!reported.ok)
            return reported;
        refresh_agents(
            *session, std::chrono::steady_clock::now());
        return session->agent_service->handle(
            "agent.get",
            { { "instance_id", *instance_id } });
    }
    if (request.method == "pane.read")
    {
        ControlMethodResult failure;
        ServerSession* session = resolve_session(request.params,
            SessionServiceNeed::Topology,
            "Server Session is unavailable.", failure);
        if (!session)
            return failure;
        if (!request.params.contains("pane_id")
            || !request.params["pane_id"].is_string())
        {
            return ControlMethodResult::error(
                "invalid_params",
                "pane.read requires a non-empty 'pane_id'.");
        }
        int max_lines = 50;
        if (request.params.contains("lines"))
        {
            const auto& lines = request.params["lines"];
            if (!lines.is_number_integer())
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    "'lines' must be between 1 and 200.");
            }
            const bool valid_lines = lines.is_number_unsigned()
                ? lines.get<uint64_t>() >= 1
                    && lines.get<uint64_t>() <= 200
                : lines.get<int64_t>() >= 1
                    && lines.get<int64_t>() <= 200;
            if (!valid_lines)
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    "'lines' must be between 1 and 200.");
            }
            max_lines = lines.is_number_unsigned()
                ? static_cast<int>(lines.get<uint64_t>())
                : static_cast<int>(lines.get<int64_t>());
        }
        if (max_lines < 1 || max_lines > 200)
        {
            return ControlMethodResult::error(
                "invalid_params",
                "'lines' must be between 1 and 200.");
        }
        const std::string pane_id
            = request.params["pane_id"].get<std::string>();
        for (const auto& [terminal_id, endpoint] : session->terminals)
        {
            const TopologySnapshot& topology
                = session->topology_service->snapshot();
            for (const auto& space : topology.spaces)
            {
                for (const auto& tab : space.tabs)
                {
                    const auto pane = std::ranges::find_if(
                        tab.panes,
                        [&](const TopologyPane& value) {
                            return value.pane_id == pane_id
                                && value.terminal_id
                                == terminal_id;
                        });
                    if (pane == tab.panes.end())
                        continue;
                    const auto observation
                        = endpoint.runtime
                              ->capture_agent_observation(
                                  max_lines, 64 * 1024);
                    if (!observation)
                    {
                        return ControlMethodResult::error(
                            "unsupported",
                            "Pane does not expose readable terminal text.");
                    }
                    return ControlMethodResult::success({
                        { "pane_id", pane_id },
                        { "space_id", space.space_id },
                        { "tab_id", tab.tab_id },
                        { "lines",
                            observation->bottom_rows },
                        { "output_generation",
                            observation
                                ->output_generation },
                    });
                }
            }
        }
        return ControlMethodResult::error(
            "not_found", "Pane not found.");
    }
    if (request.method == "server.shutdown")
    {
        if (!request.params.is_object())
        {
            return ControlMethodResult::error(
                "invalid_params",
                "Server shutdown parameters must be an object.");
        }
        const auto confirmation
            = request.params.find("confirm_live_terminals");
        if (confirmation != request.params.end()
            && !confirmation->is_boolean())
        {
            return ControlMethodResult::error(
                "invalid_params",
                "confirm_live_terminals must be a boolean.");
        }
        const bool confirmed
            = confirmation != request.params.end()
            && confirmation->get<bool>();
        size_t live_terminals = 0;
        for (const auto& [session_id, session] : sessions)
        {
            (void)session_id;
            live_terminals += static_cast<size_t>(
                std::ranges::count_if(
                    session->terminals,
                    [](const auto& item) {
                        return item.second.runtime
                            ->is_running();
                    }));
        }
        if (live_terminals > 0 && !confirmed)
        {
            return ControlMethodResult::error(
                "confirmation_required",
                "The Draxul server has "
                    + std::to_string(live_terminals)
                    + " live terminal"
                    + (live_terminals == 1 ? "" : "s")
                    + ". Confirm shutdown to stop them.");
        }
        request_stop();
        return ControlMethodResult::success({
            { "stopping", true },
            { "server_pid", pid },
            { "server_epoch", epoch_value },
        });
    }
    return ControlMethodResult::error(
        "unknown_method", "Unknown Draxul server method.");
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
    control.stop();
    remove_starting_marker();
    started = false;
}

ServerKernel::ServerKernel(ServerKernelOptions options)
    : impl_(std::make_unique<Impl>(std::move(options)))
{
}

ServerKernel::~ServerKernel()
{
    stop();
}

ServerStartResult ServerKernel::start()
{
    return impl_->start();
}

int ServerKernel::run_until_stopped()
{
    return impl_->run_until_stopped();
}

void ServerKernel::request_stop()
{
    impl_->request_stop();
}

void ServerKernel::stop()
{
    impl_->stop();
}

bool ServerKernel::running() const
{
    return impl_->started;
}

const std::string& ServerKernel::epoch() const
{
    return impl_->epoch_value;
}

uint64_t ServerKernel::process_id() const
{
    return impl_->pid;
}

ServerStatusSnapshot ServerKernel::status_snapshot() const
{
    return impl_->status_snapshot();
}

} // namespace draxul
