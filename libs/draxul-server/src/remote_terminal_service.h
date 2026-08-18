#pragma once

#include "remote_terminal_runtime.h"

#include <draxul/control_plane.h>
#include <draxul/remote_terminal_protocol.h>
#include <draxul/session_protocol.h>

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace draxul
{

struct RemoteTerminalServiceOptions
{
    std::string method_prefix;
    std::string server_epoch;
    std::string pane_id;
    std::string terminal_id;
    std::string name;
    std::string preferred_controller_client_id;
    std::chrono::milliseconds loop_latency_warning_threshold{
        std::chrono::milliseconds(100)
    };
    // Defaults are the production wire/resource limits. Tests can lower them
    // to exercise degradation and resync behavior without constructing
    // maximum-size terminal frames.
    size_t subscriber_queue_byte_limit
        = kRemoteTerminalSubscriberQueueByteLimit;
    size_t poll_payload_budget
        = kControlMaxMessageBytes - 64 * 1024;
    std::function<void(uint64_t)> prepare_restart_generation;
};

struct RemoteTerminalPollSlice
{
    std::optional<RemoteTerminalAttach> attach;
    std::vector<RemoteTerminalEvent> events;
    size_t payload_bytes = 0;
    bool suspended = false;
    bool resync = false;
    bool more = false;
    bool oversized = false;
    std::string error_code;
    std::string error_message;

    bool ok() const noexcept
    {
        return error_code.empty();
    }
};

class RemoteTerminalService
{
public:
    RemoteTerminalService(
        RemoteTerminalServiceOptions options, IRemoteTerminalRuntime& runtime);

    bool handles(std::string_view method) const;
    ControlMethodResult handle(
        std::string_view method, const nlohmann::json& params);
    RemoteTerminalPollSlice poll_subscription(
        std::string_view client_id,
        const SessionTerminalSubscription& subscription,
        size_t soft_byte_budget, size_t hard_byte_budget,
        size_t event_limit);
    void disconnect_client(std::string_view client_id);
    void pump();
    bool started() const;
    bool ensure_runtime_started(std::string& error);
    bool restart_runtime(std::string& error);
    uint64_t generation() const noexcept
    {
        return generation_;
    }

private:
    struct EncodedEvent
    {
        RemoteTerminalEvent event;
        nlohmann::json encoded;
        size_t bytes = 0;
    };

    struct Subscriber
    {
        std::string client_id;
        std::deque<std::shared_ptr<const EncodedEvent>> events;
        size_t queued_bytes = 0;
        bool needs_resync = false;
        bool suspended = false;
    };

    static std::string subscriber_key(
        std::string_view client_id, uint64_t subscription_id);
    Subscriber* find_subscriber(
        std::string_view client_id, uint64_t subscription_id);
    RemoteTerminalAttach make_attach(
        const std::shared_ptr<const EncodedEvent>& state) const;
    RemoteTerminalPollSlice poll_delivery(Subscriber& delivery,
        const RemoteTerminalVersion& requested,
        size_t soft_byte_budget, size_t hard_byte_budget,
        size_t event_limit, bool resync_stale_sequence);

    bool read_client_id(
        const nlohmann::json& params, std::string& client_id) const;
    bool read_version(
        const nlohmann::json& params, RemoteTerminalVersion& version) const;
    bool mutation_key(const nlohmann::json& params,
        std::string_view operation, std::string& key) const;
    std::optional<ControlMethodResult> cached_mutation(
        const std::string& key) const;
    ControlMethodResult remember_mutation(
        std::string key, ControlMethodResult result);
    RemoteTerminalVersion version() const;
    RemoteTerminalEvent snapshot_event();
    RemoteTerminalEvent make_delta_event();
    RemoteTerminalEvent make_controller_event();
    RemoteTerminalEvent make_clipboard_event(std::string text);
    std::shared_ptr<const EncodedEvent> encode_event(
        RemoteTerminalEvent event, bool degrade_to_poll_budget);
    void require_resync(Subscriber& subscriber);
    void broadcast(const RemoteTerminalEvent& event);
    void publish_runtime_updates(bool terminal_changed);

    ControlMethodResult attach(const nlohmann::json& params);
    ControlMethodResult suspend(const nlohmann::json& params);
    ControlMethodResult resume(const nlohmann::json& params);
    ControlMethodResult poll(const nlohmann::json& params);
    ControlMethodResult input(const nlohmann::json& params);
    ControlMethodResult resize(const nlohmann::json& params);
    ControlMethodResult take_control(const nlohmann::json& params);
    ControlMethodResult disconnect(const nlohmann::json& params);
    ControlMethodResult restart(const nlohmann::json& params);
    ControlMethodResult read_scrollback(const nlohmann::json& params);
    ControlMethodResult metrics() const;

    RemoteTerminalServiceOptions options_;
    IRemoteTerminalRuntime& runtime_;
    uint64_t generation_ = 1;
    uint64_t sequence_ = 0;
    bool started_ = false;
    bool published_process_running_ = false;
    std::optional<int> published_exit_code_;
    std::string controller_client_id_;
    std::string preferred_controller_client_id_;
    std::unordered_map<std::string, Subscriber> subscribers_;
    std::unordered_map<std::string, ControlMethodResult>
        completed_mutations_;
    std::deque<std::string> completed_mutation_order_;
    uint64_t snapshot_frames_ = 0;
    uint64_t snapshot_bytes_ = 0;
    uint64_t delta_frames_ = 0;
    uint64_t delta_bytes_ = 0;
    uint64_t delta_cells_ = 0;
    uint64_t full_frame_cells_ = 0;
    uint64_t resyncs_ = 0;
    uint64_t degraded_frames_ = 0;
    uint64_t oversized_queue_events_ = 0;
    uint64_t suspension_count_ = 0;
    uint64_t resume_count_ = 0;
    uint64_t avoided_delta_encodes_ = 0;
    uint64_t suppressed_clipboard_events_ = 0;
    uint64_t scrollback_requests_ = 0;
    uint64_t scrollback_rows_served_ = 0;
    uint64_t loop_latency_warnings_ = 0;
    uint64_t max_loop_interval_ms_ = 0;
    size_t max_queue_depth_ = 0;
    size_t max_queue_bytes_ = 0;
    std::optional<std::chrono::steady_clock::time_point>
        last_pump_at_;
};

} // namespace draxul
