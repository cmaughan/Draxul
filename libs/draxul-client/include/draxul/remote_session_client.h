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
};

struct RemoteTopologyCommandCompletion
{
    TopologyCommand command;
    bool ok = false;
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
    std::optional<ServerAgentSnapshot> agents;
    std::vector<RemoteTopologyCommandCompletion> commands;
    std::vector<RemoteStatusCompletion> statuses;
    std::optional<std::string> topology_error;
    std::optional<std::string> agent_error;
    std::optional<ClientRecoverySnapshot> recovery;
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

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace draxul
