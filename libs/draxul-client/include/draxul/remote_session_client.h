#pragma once

#include <draxul/agent_protocol.h>
#include <draxul/client_recovery.h>
#include <draxul/server_client.h>
#include <draxul/topology_protocol.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace draxul
{

struct RemoteSessionClientOptions
{
    std::filesystem::path runtime_directory;
    std::string client_id;
    std::string session_id = "default";
    std::function<void()> wake_consumer;
    std::shared_ptr<ClientRecoveryState> recovery;
    // The Session coordinator supplies topology and agent snapshots from its
    // negotiated event stream or session.poll fallback. The worker still
    // owns commands and one-shot status calls.
    bool externally_fed = false;
};

struct RemoteSessionPollRevisions
{
    uint64_t topology = 0;
    uint64_t agents = 0;
};

struct RemoteTopologyCommandCompletion
{
    TopologyCommand command;
    bool ok = false;
    std::string created_id;
    std::optional<TopologySnapshot> snapshot;
    std::string error_code;
    std::string error_message;
};

struct RemoteStatusCompletion
{
    uint64_t request_id = 0;
    ServerStatusResult result;
};

struct RemoteSessionPublishedState
{
    std::optional<TopologySnapshot> topology;
    std::string topology_server_epoch;
    std::optional<ServerAgentSnapshot> agents;
    std::string agent_server_epoch;
    std::vector<RemoteTopologyCommandCompletion> commands;
    std::vector<RemoteStatusCompletion> statuses;
    std::optional<std::string> topology_error;
    std::optional<std::string> agent_error;
    std::optional<ClientRecoverySnapshot> recovery;
    std::vector<std::string> persistence_warnings;
    // Allows the UI projection to accept a lower topology revision after a
    // real server restart.
    bool server_epoch_changed = false;
};

// Owns every recurring shared-Session request on a worker thread. The GUI
// enqueues intent and consumes immutable published state; it never waits for a
// topology/agent/status round trip.
class RemoteSessionClient
{
public:
    explicit RemoteSessionClient(RemoteSessionClientOptions options);
    ~RemoteSessionClient();
    RemoteSessionClient(const RemoteSessionClient&) = delete;
    RemoteSessionClient& operator=(const RemoteSessionClient&) = delete;

    bool start();
    void stop();
    bool enqueue(TopologyCommand command);
    std::optional<uint64_t> request_status();
    std::optional<RemoteSessionPublishedState> take_published_state();
    void acknowledge_topology(
        std::string_view server_epoch, uint64_t revision);
    void acknowledge_agents(
        std::string_view server_epoch, uint64_t revision);

    // Thread-safe ingress used by the UI-scoped Session stream/poll worker.
    RemoteSessionPollRevisions session_poll_revisions() const;
    void accept_session_poll_topology(
        std::string server_epoch, TopologySnapshot snapshot,
        std::string_view recovery_channel = "session.poll");
    void accept_session_poll_agents(
        std::string server_epoch, ServerAgentSnapshot snapshot,
        std::string_view recovery_channel = "session.poll");
    void accept_session_poll_epoch(std::string server_epoch,
        std::string_view recovery_channel = "session.poll");
    void invalidate_session_poll_cursors(std::string server_epoch,
        std::string_view recovery_channel = "session.poll");
    void accept_session_poll_error(
        std::string channel, std::string error,
        std::string_view recovery_channel = "session.poll");
    void enable_legacy_polling();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace draxul
