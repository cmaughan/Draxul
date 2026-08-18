#pragma once

#include <draxul/client_recovery.h>
#include <draxul/control_plane.h>
#include <draxul/remote_terminal_protocol.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

class RemoteSessionClient;

struct RemoteSessionCoordinatorOptions
{
    std::filesystem::path runtime_directory;
    std::string client_id;
    std::string session_id = "default";
    std::string expected_server_epoch;
    std::string method_prefix = "fake";
    std::shared_ptr<ClientRecoveryState> recovery;
    bool presentation_suspend_supported = false;
    bool session_stream_supported = false;
    bool session_stream_commands_supported = false;
    bool session_poll_supported = false;
    // Required when either multiplexed Session transport is enabled. App owns
    // this client for longer than the coordinator and stops the coordinator
    // first.
    RemoteSessionClient* session_client = nullptr;
    std::function<void()> wake_consumer;
};

// Immutable terminal presentation handed from a coordinator-owned transport
// worker to its UI registration. Grid and renderer mutation remain on the UI
// thread which consumes this value.
struct RemoteTerminalPublishedState
{
    TerminalSemanticSnapshot snapshot;
    std::optional<TerminalDirtySnapshot> grid_update;
    std::optional<RemoteTerminalScrollbackPage> scrollback_page;
    uint64_t scroll_offset = 0;
    uint64_t scrollback_total = 0;
    std::string controller_client_id;
    std::string display_name;
    bool process_running = true;
    std::optional<int> exit_code;
    std::optional<std::string> clipboard_write;
    std::chrono::microseconds attach_latency{ 0 };
    uint64_t visibility_generation = 1;
};

enum class RemoteSessionTransportKind
{
    Stream,
    SessionPoll,
    Legacy,
};

struct RemoteSessionTransportSnapshot
{
    RemoteSessionTransportKind transport
        = RemoteSessionTransportKind::Legacy;
    bool stream_commands = false;
    ClientRecoverySnapshot recovery;
    ClientRecoveryMetricsSnapshot recovery_metrics;
};

// UI-scoped owner for remote terminal transports. A negotiated event stream
// is preferred, with one recurring Session poll worker as its fallback;
// older servers retain the Phase-1 per-terminal workers. Registration,
// command, recovery, and mailbox behavior is intentionally identical across
// all three backends.
class RemoteSessionCoordinator
{
public:
    class Registration
    {
    public:
        Registration();
        ~Registration();
        Registration(Registration&& other) noexcept;
        Registration& operator=(Registration&& other) noexcept;
        Registration(const Registration&) = delete;
        Registration& operator=(const Registration&) = delete;

        explicit operator bool() const noexcept;
        uint64_t id() const noexcept;

        bool enqueue_input(std::string_view text);
        bool enqueue_input_chunks(std::vector<std::string> chunks);
        bool enqueue_resize(int cols, int rows);
        bool enqueue_take_control();
        bool enqueue_scroll(int rows);
        bool enqueue_scroll_to_live();

        // Returns the current generation. The generation advances on every
        // visibility transition so a late result cannot repaint a hidden or
        // newly resumed pane.
        uint64_t set_presentation_visible(bool visible);
        bool presentation_visible() const;
        uint64_t visibility_generation() const;

        std::optional<RemoteTerminalPublishedState>
        take_published_state();
        std::string take_error();
        bool running() const;
        std::string last_error_code() const;
        void reset();

    private:
        friend class RemoteSessionCoordinator;
        class State;
        explicit Registration(std::unique_ptr<State> state);
        std::unique_ptr<State> state_;
    };

    explicit RemoteSessionCoordinator(
        RemoteSessionCoordinatorOptions options);
    ~RemoteSessionCoordinator();
    RemoteSessionCoordinator(const RemoteSessionCoordinator&) = delete;
    RemoteSessionCoordinator& operator=(
        const RemoteSessionCoordinator&) = delete;

    bool start();
    void stop();
    Registration register_terminal(std::string terminal_id);

    // Uses the attached UI's correlated command stream when it is active.
    // A missing result means the caller should use its short-control fallback.
    std::optional<ControlClientResult> request_stream_command(
        std::string method, nlohmann::json params,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(600));
    RemoteSessionTransportSnapshot transport_snapshot() const;

    // The UI calls this after draining all ready registrations. Publications
    // racing with the acknowledgement retain their ready marker and schedule
    // another wake, so no edge can be lost.
    void acknowledge_wake();

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace draxul
