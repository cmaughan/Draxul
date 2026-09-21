#pragma once

#include <draxul/session_protocol.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace draxul::detail
{

// Worker-owned policy for the multiplexed Session stream. This type contains
// no connection, thread, registration, projection, or publication objects.
// Callers translate its value decisions into those side effects.
class SessionStreamPolicy
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using CommandToken = uint64_t;
    using RegistrationId = uint64_t;

    enum class CommandOwner
    {
        Generic,
        Terminal,
        Topology,
    };

    struct Command
    {
        CommandToken token = 0;
        CommandOwner owner = CommandOwner::Generic;
        RegistrationId registration_id = 0;
        uint64_t topology_revision = 0;
        int retry_count = 0;
    };

    enum class CommandResultAction
    {
        Complete,
        RetryOnShortControl,
        WaitForNewerTopology,
        ObsoleteRegistration,
    };

    struct CommandResultDecision
    {
        CommandResultAction action = CommandResultAction::Complete;
        Command command;
        SessionStreamCommandResult result;
    };

    enum class FrameAction
    {
        Heartbeat,
        Events,
        CommandResult,
        Failure,
    };

    struct FrameDecision
    {
        FrameAction action = FrameAction::Heartbeat;
        std::optional<SessionPollResponse> events;
        std::optional<CommandResultDecision> command;
        std::string failure_code;
        std::string failure_message;

        explicit operator bool() const noexcept
        {
            return action != FrameAction::Failure;
        }
    };

    struct PreparedUpdate
    {
        SessionPollRequest poll;
    };

    struct RetryCandidate
    {
        Command command;
        uint64_t topology_revision = 0;
    };

    static constexpr size_t kCommandsPerWriteTurn = 8;
    static constexpr size_t kSharedCommandsPerWriteTurn
        = kCommandsPerWriteTurn / 2;

    void activate(std::string server_epoch,
        SessionPollRequest initial_poll,
        std::chrono::milliseconds heartbeat_timeout,
        TimePoint now);
    void reset();

    std::optional<PreparedUpdate> prepare_update(
        SessionPollRequest current);
    void commit_update(const PreparedUpdate& update);
    bool acknowledgement_pending() const noexcept;

    bool track_command_write(uint64_t request_id, Command command);
    std::optional<CommandResultDecision> accept_command_result(
        SessionStreamCommandResult result,
        std::span<const RegistrationId> active_registrations);
    FrameDecision accept_frame(SessionStreamServerFrame frame,
        TimePoint now,
        std::span<const RegistrationId> active_registrations = {});
    std::optional<RetryCandidate> take_topology_retry(
        uint64_t observed_revision);

    // Returns sent/deferred command tokens in their original send order and
    // clears their policy state. The coordinator appends never-sent commands,
    // then applies owner-specific fallback effects.
    std::vector<CommandToken> abandon_sent_commands();

    size_t shared_command_budget(size_t already_sent) const noexcept;
    std::vector<RegistrationId> terminal_probe_order(
        std::span<const RegistrationId> registrations) const;
    void advance_terminal_rotation(size_t registration_count,
        size_t visited) noexcept;

    bool heartbeat_expired(TimePoint now) const noexcept;
    std::chrono::milliseconds heartbeat_time_remaining(
        TimePoint now) const noexcept;

    size_t pending_command_count() const noexcept;
    size_t deferred_command_count() const noexcept;
    uint64_t last_frame_serial() const noexcept;
    uint64_t last_event_request_serial() const noexcept;

private:
    struct SentCommand
    {
        uint64_t request_id = 0;
        uint64_t send_order = 0;
        Command command;
    };

    static bool same_session_state(const SessionPollRequest& lhs,
        const SessionPollRequest& rhs) noexcept;
    static bool registration_is_active(RegistrationId registration_id,
        std::span<const RegistrationId> active_registrations);
    static bool retries_on_short_control(std::string_view error_code);
    FrameDecision fail(std::string code, std::string message) const;
    uint64_t next_request_serial() noexcept;

    std::string server_epoch_;
    std::optional<SessionPollRequest> last_poll_;
    uint64_t next_request_serial_ = 1;
    uint64_t last_frame_serial_ = 0;
    uint64_t last_event_request_serial_ = 0;
    bool acknowledgement_pending_ = false;
    std::chrono::milliseconds heartbeat_timeout_{ 0 };
    TimePoint last_frame_at_{};
    std::vector<SentCommand> pending_commands_;
    std::vector<SentCommand> deferred_topology_commands_;
    uint64_t next_send_order_ = 1;
    size_t terminal_rotation_ = 0;
};

} // namespace draxul::detail
