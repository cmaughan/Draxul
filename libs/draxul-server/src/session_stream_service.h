#pragma once

#include <draxul/control_plane.h>
#include <draxul/session_protocol.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace draxul
{

struct SessionStreamServiceOptions
{
    std::filesystem::path runtime_directory;
    std::string server_epoch;
    std::chrono::milliseconds heartbeat_interval{
        kSessionStreamDefaultHeartbeatIntervalMs
    };
    size_t max_queue_bytes = kSessionStreamDefaultQueueBytes;
    std::function<void()> wake_state_thread;
};

struct SessionStreamServiceStats
{
    size_t control_reserve_bytes = 0;
    size_t pending_command_count = 0;
    size_t pending_command_bytes = 0;
    size_t queued_control_bytes = 0;
    size_t queued_event_bytes = 0;
    size_t peak_control_bytes = 0;
    size_t peak_event_bytes = 0;
    size_t completed_command_count = 0;
    size_t completed_command_bytes = 0;
    uint64_t commands_dispatched = 0;
    uint64_t command_replays = 0;
    uint64_t command_conflicts = 0;
    uint64_t command_rejections = 0;

    bool operator==(const SessionStreamServiceStats&) const = default;
};

// Kernel-wide owner of the one raw stream endpoint. Tickets route accepted
// connections to per-Session polling semantics without creating an OS
// listener per Session. pump() is called only by the server state thread.
class SessionStreamService
{
public:
    using Poll = std::function<ControlMethodResult(std::string_view session_id,
        std::string_view client_id, const SessionPollRequest& request,
        size_t payload_budget)>;
    using Touch = std::function<void(std::string_view client_id)>;
    using Dispatch = std::function<ControlMethodResult(
        std::string_view session_id, std::string_view client_id,
        const SessionStreamCommand& command)>;

    explicit SessionStreamService(SessionStreamServiceOptions options);
    ~SessionStreamService();
    SessionStreamService(const SessionStreamService&) = delete;
    SessionStreamService& operator=(const SessionStreamService&) = delete;

    bool start(std::string& error);
    void stop();
    ControlMethodResult open(const nlohmann::json& params,
        std::string_view authenticated_client_id);
    void pump(const Poll& poll, const Touch& touch = {},
        const Dispatch& dispatch = {});
    void disconnect_client(std::string_view client_id);
    const std::string& endpoint() const;
    size_t connection_count() const;
    SessionStreamServiceStats stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace draxul
