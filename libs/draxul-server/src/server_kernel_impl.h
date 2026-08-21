#pragma once

#include "fake_terminal_runtime.h"
#include "remote_terminal_service.h"
#include "server_terminal_runtime.h"
#include "session_poll_service.h"
#include "session_stream_service.h"
#include "topology_service.h"

#include <draxul/control_plane.h>
#include "server_agent_service.h"
#include <draxul/server_kernel.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace draxul
{

// Cross-thread wake state shared with control and terminal callbacks. Callbacks
// only set an atomic edge and notify; the server state thread owns all service
// pumping and clears the edges before waiting again.
struct ServerLoopWakeState
{
    std::mutex mutex;
    std::condition_variable condition;
    std::atomic<bool> control_work_pending = false;
    std::atomic<bool> terminal_output_pending = false;
};

// Private implementation boundary for ServerKernel. The public façade remains in
// server_kernel.h; responsibility TUs include this header while callers cannot.
//
// Ownership and concurrency invariants:
// - The run-loop thread exclusively owns sessions, services, terminal endpoints,
//   checkpoint scheduling, agent refresh, and mutation-result caches.
// - mutex protects only clients and client_sessions. Never call a service or the
//   control transport while holding it.
// - ControlServer workers enqueue requests; handle_request runs on the state thread
//   through process_pending. request_stop and wake callbacks are the cross-thread
//   entry points and communicate through atomics plus ServerLoopWakeState.
// - A checkpoint worker mutates only its CheckpointTask under that task's mutex;
//   the state thread collects the result and updates ServerSession state.
// - loop_wake and terminal_resource_budget are shared so callbacks/tasks cannot
//   outlive their synchronization or accounting state during ordered shutdown.
// - Per-client tokens/activity establish authentication leases. Terminal runtime
//   generations and topology revisions gate exit cleanup, while bounded completed
//   mutation caches make retried agent requests idempotent.
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
        std::string ui_control_id;
        std::string ui_control_runtime_directory;
        bool token_required = false;
    };

    enum class SessionServiceNeed
    {
        None,
        Topology,
        Agent,
        TopologyAndAgent,
    };

    explicit Impl(ServerKernelOptions value);

    // Lifecycle, publication, and state-loop ownership.
    ServerStartResult start();
    int run_until_stopped();
    void request_stop();
    void stop();
    ServerStatusSnapshot status_snapshot() const;
    void publish_starting_marker();
    bool published_identity_matches() const;
    void publish_failure_marker(std::string_view reason);
    void remove_failure_marker();
    void remove_starting_marker();
    void remove_all_starting_markers();

    // Request routing and authenticated client leases.
    ControlMethodResult handle_request(const ControlRequest& request);
    ControlMethodResult poll_session(std::string_view session_id,
        std::string_view client_id, const SessionPollRequest& request,
        size_t payload_budget = kSessionPollPayloadBudget);
    SessionPollBuildResult build_session_poll(
        std::string_view session_id,
        std::string_view client_id, const SessionPollRequest& request,
        size_t payload_budget = kSessionPollPayloadBudget);
    ControlMethodResult dispatch_stream_command(std::string_view session_id,
        std::string_view client_id,
        const SessionStreamCommand& command);
    static std::string random_epoch();
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
    size_t active_clients_for_session(std::string_view session_id);
    void forget_session_clients(std::string_view session_id);
    void prune_inactive_clients(
        std::chrono::steady_clock::time_point now);

    // Session restore, lookup, persistence, and lifecycle.
    bool prepare_session_restore(std::string& error);
    bool initialize_services(std::string& error);
    bool initialize_session(
        std::string_view session_id, std::string& error);
    ServerSession* ensure_session(
        std::string_view session_id, std::string& error);
    // Shared preamble for session-scoped control methods: reads the session id,
    // ensures the session, and verifies the requested services.
    ServerSession* resolve_session(const nlohmann::json& params,
        SessionServiceNeed need, std::string_view unavailable_message,
        ControlMethodResult& failure);
    ControlMethodResult delete_session(const nlohmann::json& params);
    ControlMethodResult delete_all_sessions(const nlohmann::json& params);
    ControlMethodResult rename_session(const nlohmann::json& params);
    void reset_services();
    bool checkpoint_session(
        std::string_view session_id, std::string& error);
    void collect_checkpoint_results();
    void wait_for_checkpoint_tasks(
        std::chrono::steady_clock::time_point deadline);
    bool read_session_id(const nlohmann::json& params,
        std::string& session_id, std::string& error) const;

    // Terminal and managed-agent runtime ownership.
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
    std::optional<std::string> create_managed_agent_terminal(
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

    ServerKernelOptions options;
    std::shared_ptr<ServerTerminalResourceBudget>
        terminal_resource_budget;
    AgentDefinitionRegistry agent_definitions;
    ControlServer control;
    std::unique_ptr<SessionStreamService> session_stream;
    std::string epoch_value;
    uint64_t pid = 0;
    std::string process_start_identity;
    std::chrono::steady_clock::time_point started_at{};
    std::atomic<bool> started = false;
    std::atomic<bool> stop_requested = false;

    // Only clients and client_sessions are protected by mutex. All remaining
    // state below is owned by the run-loop thread.
    mutable std::mutex mutex;
    std::shared_ptr<ServerLoopWakeState> loop_wake
        = std::make_shared<ServerLoopWakeState>();
    std::unordered_map<std::string, ClientRegistration> clients;
    std::unordered_map<std::string,
        std::unordered_set<std::string>> client_sessions;
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
        std::unique_ptr<SessionPollService> poll_service;
        std::unordered_map<std::string, ServerTerminalEndpoint> terminals;
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

    std::unordered_map<std::string, std::unique_ptr<ServerSession>> sessions;
    std::vector<std::string> unassigned_restore_warnings;
};

} // namespace draxul
