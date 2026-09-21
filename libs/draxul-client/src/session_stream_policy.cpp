#include "session_stream_policy.h"

#include <algorithm>
#include <utility>

namespace draxul::detail
{

void SessionStreamPolicy::activate(std::string server_epoch,
    SessionPollRequest initial_poll,
    std::chrono::milliseconds heartbeat_timeout,
    TimePoint now)
{
    reset();
    server_epoch_ = std::move(server_epoch);
    next_request_serial_ = initial_poll.request_serial;
    if (++next_request_serial_ == 0)
        next_request_serial_ = 1;
    last_poll_ = std::move(initial_poll);
    heartbeat_timeout_ = heartbeat_timeout;
    last_frame_at_ = now;
}

void SessionStreamPolicy::reset()
{
    server_epoch_.clear();
    last_poll_.reset();
    next_request_serial_ = 1;
    last_frame_serial_ = 0;
    last_event_request_serial_ = 0;
    acknowledgement_pending_ = false;
    heartbeat_timeout_ = std::chrono::milliseconds::zero();
    last_frame_at_ = {};
    pending_commands_.clear();
    deferred_topology_commands_.clear();
    next_send_order_ = 1;
    terminal_rotation_ = 0;
}

std::optional<SessionStreamPolicy::PreparedUpdate>
SessionStreamPolicy::prepare_update(SessionPollRequest current)
{
    if (!acknowledgement_pending_ && last_poll_
        && same_session_state(current, *last_poll_))
    {
        return std::nullopt;
    }
    current.request_serial = next_request_serial();
    return PreparedUpdate{ .poll = std::move(current) };
}

void SessionStreamPolicy::commit_update(const PreparedUpdate& update)
{
    last_poll_ = update.poll;
    acknowledgement_pending_ = false;
}

bool SessionStreamPolicy::acknowledgement_pending() const noexcept
{
    return acknowledgement_pending_;
}

bool SessionStreamPolicy::track_command_write(
    uint64_t request_id, Command command)
{
    const auto conflicts = [&](const SentCommand& sent) {
        return sent.request_id == request_id
            || sent.command.token == command.token;
    };
    if (request_id == 0 || command.token == 0
        || (command.owner == CommandOwner::Terminal
            && command.registration_id == 0)
        || std::ranges::any_of(pending_commands_, conflicts)
        || std::ranges::any_of(
            deferred_topology_commands_, conflicts))
    {
        return false;
    }
    const uint64_t send_order = next_send_order_;
    if (++next_send_order_ == 0)
        next_send_order_ = 1;
    pending_commands_.push_back({
        .request_id = request_id,
        .send_order = send_order,
        .command = std::move(command),
    });
    return true;
}

std::optional<SessionStreamPolicy::CommandResultDecision>
SessionStreamPolicy::accept_command_result(
    SessionStreamCommandResult result,
    std::span<const RegistrationId> active_registrations)
{
    const auto found = std::ranges::find(pending_commands_,
        result.request_id, &SentCommand::request_id);
    if (found == pending_commands_.end())
        return std::nullopt;

    SentCommand sent = std::move(*found);
    pending_commands_.erase(found);
    CommandResultAction action = CommandResultAction::Complete;
    const bool obsolete_registration
        = sent.command.owner == CommandOwner::Terminal
        && !registration_is_active(sent.command.registration_id,
            active_registrations);
    if (obsolete_registration)
    {
        action = CommandResultAction::ObsoleteRegistration;
    }
    else if (!result.ok
        && retries_on_short_control(result.error_code))
    {
        action = CommandResultAction::RetryOnShortControl;
    }
    else if (sent.command.owner == CommandOwner::Topology
        && !result.ok && result.error_code == "revision_conflict"
        && sent.command.retry_count == 0)
    {
        ++sent.command.retry_count;
        deferred_topology_commands_.push_back(sent);
        action = CommandResultAction::WaitForNewerTopology;
    }
    return CommandResultDecision{
        .action = action,
        .command = std::move(sent.command),
        .result = std::move(result),
    };
}

SessionStreamPolicy::FrameDecision SessionStreamPolicy::accept_frame(
    SessionStreamServerFrame frame, TimePoint now,
    std::span<const RegistrationId> active_registrations)
{
    if (frame.server_epoch != server_epoch_
        || frame.frame_serial != last_frame_serial_ + 1)
    {
        return fail(frame.server_epoch != server_epoch_
                ? "stale_epoch"
                : "invalid_session_stream",
            "The Session event stream identity or ordering changed.");
    }

    last_frame_serial_ = frame.frame_serial;
    last_frame_at_ = now;
    if (frame.kind == SessionStreamServerFrameKind::Error)
    {
        return fail(std::move(frame.error_code),
            std::move(frame.error_message));
    }
    if (frame.kind == SessionStreamServerFrameKind::CommandResult)
    {
        if (!frame.command_result)
        {
            return fail("invalid_session_stream",
                "The Session stream command response does not match an outstanding request.");
        }
        auto command = accept_command_result(
            std::move(*frame.command_result), active_registrations);
        if (!command)
        {
            return fail("invalid_session_stream",
                "The Session stream command response does not match an outstanding request.");
        }
        return {
            .action = FrameAction::CommandResult,
            .command = std::move(*command),
        };
    }
    if (frame.kind == SessionStreamServerFrameKind::Events)
    {
        if (!frame.events || !last_poll_
            || frame.events->server_epoch != frame.server_epoch
            || frame.events->request_serial
                > last_poll_->request_serial
            || frame.events->request_serial
                <= last_event_request_serial_)
        {
            return fail("invalid_session_stream",
                "The Session event batch does not match the active stream state.");
        }
        last_event_request_serial_ = frame.events->request_serial;
        acknowledgement_pending_ = true;
        return {
            .action = FrameAction::Events,
            .events = std::move(frame.events),
        };
    }
    return { .action = FrameAction::Heartbeat };
}

std::optional<SessionStreamPolicy::RetryCandidate>
SessionStreamPolicy::take_topology_retry(uint64_t observed_revision)
{
    if (deferred_topology_commands_.empty())
        return std::nullopt;
    auto& deferred = deferred_topology_commands_.front();
    if (observed_revision <= deferred.command.topology_revision)
        return std::nullopt;
    SentCommand retry = std::move(deferred);
    deferred_topology_commands_.erase(
        deferred_topology_commands_.begin());
    retry.command.topology_revision = observed_revision;
    return RetryCandidate{
        .command = std::move(retry.command),
        .topology_revision = observed_revision,
    };
}

std::vector<SessionStreamPolicy::CommandToken>
SessionStreamPolicy::abandon_sent_commands()
{
    std::vector<SentCommand> commands;
    commands.reserve(pending_commands_.size()
        + deferred_topology_commands_.size());
    for (auto& command : pending_commands_)
        commands.push_back(std::move(command));
    for (auto& command : deferred_topology_commands_)
        commands.push_back(std::move(command));
    pending_commands_.clear();
    deferred_topology_commands_.clear();
    std::ranges::sort(commands, {}, &SentCommand::send_order);

    std::vector<CommandToken> result;
    result.reserve(commands.size());
    for (const auto& command : commands)
        result.push_back(command.command.token);
    return result;
}

size_t SessionStreamPolicy::shared_command_budget(
    size_t already_sent) const noexcept
{
    return already_sent >= kSharedCommandsPerWriteTurn
        ? 0
        : kSharedCommandsPerWriteTurn - already_sent;
}

std::vector<SessionStreamPolicy::RegistrationId>
SessionStreamPolicy::terminal_probe_order(
    std::span<const RegistrationId> registrations) const
{
    std::vector<RegistrationId> result;
    result.reserve(registrations.size());
    if (registrations.empty())
        return result;
    const size_t start = terminal_rotation_ % registrations.size();
    for (size_t offset = 0; offset < registrations.size(); ++offset)
        result.push_back(registrations[(start + offset)
            % registrations.size()]);
    return result;
}

void SessionStreamPolicy::advance_terminal_rotation(
    size_t registration_count, size_t visited) noexcept
{
    if (registration_count == 0)
        return;
    const size_t start = terminal_rotation_ % registration_count;
    terminal_rotation_ = (start + std::max<size_t>(1, visited))
        % registration_count;
}

bool SessionStreamPolicy::heartbeat_expired(TimePoint now) const noexcept
{
    return heartbeat_timeout_ <= std::chrono::milliseconds::zero()
        || now - last_frame_at_ >= heartbeat_timeout_;
}

std::chrono::milliseconds SessionStreamPolicy::heartbeat_time_remaining(
    TimePoint now) const noexcept
{
    if (heartbeat_expired(now))
        return std::chrono::milliseconds::zero();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        heartbeat_timeout_ - (now - last_frame_at_));
}

size_t SessionStreamPolicy::pending_command_count() const noexcept
{
    return pending_commands_.size();
}

size_t SessionStreamPolicy::deferred_command_count() const noexcept
{
    return deferred_topology_commands_.size();
}

uint64_t SessionStreamPolicy::last_frame_serial() const noexcept
{
    return last_frame_serial_;
}

uint64_t SessionStreamPolicy::last_event_request_serial() const noexcept
{
    return last_event_request_serial_;
}

bool SessionStreamPolicy::same_session_state(
    const SessionPollRequest& lhs,
    const SessionPollRequest& rhs) noexcept
{
    return lhs.server_epoch == rhs.server_epoch
        && lhs.topology_after_revision == rhs.topology_after_revision
        && lhs.agent_after_revision == rhs.agent_after_revision
        && lhs.terminals == rhs.terminals;
}

bool SessionStreamPolicy::registration_is_active(
    RegistrationId registration_id,
    std::span<const RegistrationId> active_registrations)
{
    return std::ranges::find(active_registrations, registration_id)
        != active_registrations.end();
}

bool SessionStreamPolicy::retries_on_short_control(
    std::string_view error_code)
{
    return error_code == "command_result_too_large"
        || error_code == "unsupported_stream_command"
        || error_code == "command_unavailable";
}

SessionStreamPolicy::FrameDecision SessionStreamPolicy::fail(
    std::string code, std::string message) const
{
    return {
        .action = FrameAction::Failure,
        .failure_code = std::move(code),
        .failure_message = std::move(message),
    };
}

uint64_t SessionStreamPolicy::next_request_serial() noexcept
{
    const uint64_t result = next_request_serial_;
    if (++next_request_serial_ == 0)
        next_request_serial_ = 1;
    return result;
}

} // namespace draxul::detail
