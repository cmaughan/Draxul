#pragma once

#include "remote_terminal_service.h"

#include <draxul/control_plane.h>
#include "server_agent_service.h"
#include <draxul/session_protocol.h>
#include <draxul/topology_protocol.h>

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace draxul
{

inline constexpr size_t kSessionPollPayloadBudget
    = kControlMaxMessageBytes - 256 * 1024;
inline constexpr size_t kSessionPollTerminalQuantum = 256 * 1024;
inline constexpr size_t kSessionPollTerminalEventLimit = 16;

struct SessionPollTerminalView
{
    std::string_view terminal_id;
    RemoteTerminalService* service = nullptr;
};

// Immediate bounded aggregation over existing Session projections and terminal
// subscriber queues. This service never waits for changes and owns no terminal
// event history; it retains only per-client fairness cursors.
class SessionPollService
{
public:
    explicit SessionPollService(std::string server_epoch);

    ControlMethodResult handle(const nlohmann::json& params,
        std::string_view authenticated_client_id,
        const TopologySnapshot& topology,
        const ServerAgentSnapshot& agents,
        std::span<const SessionPollTerminalView> terminals);
    void disconnect_client(std::string_view client_id);

private:
    struct ClientSchedule
    {
        size_t next_channel = 0;
        uint64_t next_subscription_id = 0;
    };

    std::string server_epoch_;
    std::unordered_map<std::string, ClientSchedule> schedules_;
};

} // namespace draxul
