#include <draxul/client_recovery.h>

#include <draxul/server_client.h>

#include <algorithm>
#include <functional>
#include <limits>

namespace draxul
{

namespace
{

uint64_t next_jitter(uint64_t& state)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

void increment_saturated(uint64_t& value)
{
    if (value != std::numeric_limits<uint64_t>::max())
        ++value;
}

std::string bounded_label(
    std::string_view value, size_t limit, std::string_view fallback)
{
    if (value.empty())
        value = fallback;
    std::string result(value.substr(0, limit));
    for (char& ch : result)
    {
        if (static_cast<unsigned char>(ch) < 0x20 || ch == 0x7f)
            ch = '?';
    }
    return result;
}

} // namespace

ClientRecoveryState::ClientRecoveryState(std::string jitter_identity)
    : registration_nonce_(make_server_client_id())
    , jitter_identity_(std::hash<std::string>{}(jitter_identity))
{
    if (jitter_identity_ == 0)
        jitter_identity_ = 0x9e3779b97f4a7c15ULL;
}

ClientRecoveryState::ChannelState&
ClientRecoveryState::channel_locked(std::string_view channel)
{
    const std::string key(channel);
    auto [found, inserted] = channels_.try_emplace(key);
    if (inserted)
    {
        found->second.jitter_state = jitter_identity_
            ^ std::hash<std::string>{}(key);
        if (found->second.jitter_state == 0)
            found->second.jitter_state = 0xd1b54a32d192ed03ULL;
    }
    return found->second;
}

bool ClientRecoveryState::note_connected(std::string_view channel)
{
    std::lock_guard guard(mutex_);
    auto& state = channel_locked(channel);
    const bool recovered = state.outage_started_at.has_value();
    if (recovered)
        increment_saturated(state.recovery_count);
    state.phase = ClientConnectionPhase::Connected;
    state.attempts = 0;
    state.retry_delay = {};
    state.outage_started_at.reset();
    state.current_reason.clear();
    return recovered;
}

std::chrono::milliseconds ClientRecoveryState::note_failure(
    std::string_view channel, std::string_view reason)
{
    std::lock_guard guard(mutex_);
    return note_failure_locked(channel, reason, true);
}

void ClientRecoveryState::note_aggregate_failure(
    std::string_view channel, std::string_view reason)
{
    std::lock_guard guard(mutex_);
    (void)note_failure_locked(channel, reason, false);
}

std::chrono::milliseconds ClientRecoveryState::note_failure_locked(
    std::string_view channel, std::string_view reason,
    bool record_metrics)
{
    auto& state = channel_locked(channel);
    if (!state.outage_started_at)
    {
        state.outage_started_at = std::chrono::steady_clock::now();
        increment_saturated(state.interruption_count);
    }
    state.current_reason = bounded_label(
        reason, kClientRecoveryMaxReasonBytes, "unspecified");
    state.attempts = std::min<uint32_t>(state.attempts + 1, 64);
    state.phase = state.attempts == 1
        ? ClientConnectionPhase::Degraded
        : ClientConnectionPhase::Reconnecting;
    state.retry_delay = retry_delay_for(
        state.attempts, next_jitter(state.jitter_state));
    if (record_metrics)
    {
        increment_saturated(metrics_.reconnect_attempts);
        record_reason_locked(
            ClientRecoveryReasonKind::Reconnect, channel, reason);
    }
    return state.retry_delay;
}

void ClientRecoveryState::note_reconnecting(std::string_view channel)
{
    std::lock_guard guard(mutex_);
    channel_locked(channel).phase
        = ClientConnectionPhase::Reconnecting;
}

ClientRecoverySnapshot ClientRecoveryState::snapshot(
    std::string_view channel) const
{
    return snapshot_at(channel, std::chrono::steady_clock::now());
}

ClientRecoverySnapshot ClientRecoveryState::snapshot_at(
    std::string_view channel,
    std::chrono::steady_clock::time_point now) const
{
    std::lock_guard guard(mutex_);
    const auto found = channels_.find(std::string(channel));
    const ChannelState empty;
    const auto& state = found == channels_.end()
        ? empty
        : found->second;
    std::chrono::milliseconds outage_duration{ 0 };
    if (state.outage_started_at && now > *state.outage_started_at)
    {
        outage_duration
            = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - *state.outage_started_at);
    }
    return {
        .phase = state.phase,
        .attempts = state.attempts,
        .retry_delay = state.retry_delay,
        .server_epoch = server_identity_.server_epoch,
        .outage_duration = outage_duration,
        .sustained_outage
        = outage_duration >= kClientSustainedOutageThreshold,
        .interruption_count = state.interruption_count,
        .recovery_count = state.recovery_count,
        .current_reason = state.current_reason,
    };
}

void ClientRecoveryState::record_reason_locked(
    ClientRecoveryReasonKind kind, std::string_view channel,
    std::string_view reason)
{
    const std::string bounded_channel = bounded_label(
        channel, kClientRecoveryMaxChannelBytes, "unknown");
    const std::string bounded_reason = bounded_label(
        reason, kClientRecoveryMaxReasonBytes, "unspecified");
    const auto found = std::ranges::find_if(metrics_.reasons,
        [&](const ClientRecoveryReasonCount& entry) {
            return entry.kind == kind
                && entry.channel == bounded_channel
                && entry.reason == bounded_reason;
        });
    if (found != metrics_.reasons.end())
    {
        increment_saturated(found->count);
        return;
    }
    if (metrics_.reasons.size() >= kClientRecoveryMaxReasonBuckets)
    {
        increment_saturated(metrics_.reason_overflow);
        return;
    }
    metrics_.reasons.push_back({
        .kind = kind,
        .channel = bounded_channel,
        .reason = bounded_reason,
        .count = 1,
    });
}

void ClientRecoveryState::note_fallback(
    std::string_view channel, std::string_view reason)
{
    std::lock_guard guard(mutex_);
    increment_saturated(metrics_.fallbacks);
    record_reason_locked(
        ClientRecoveryReasonKind::Fallback, channel, reason);
}

void ClientRecoveryState::note_resync(
    std::string_view channel, std::string_view reason)
{
    std::lock_guard guard(mutex_);
    increment_saturated(metrics_.resyncs);
    record_reason_locked(
        ClientRecoveryReasonKind::Resync, channel, reason);
}

ClientRecoveryMetricsSnapshot ClientRecoveryState::metrics_snapshot() const
{
    std::lock_guard guard(mutex_);
    ClientRecoveryMetricsSnapshot result = metrics_;
    std::ranges::sort(result.reasons, [](const auto& left, const auto& right) {
        if (left.kind != right.kind)
            return left.kind < right.kind;
        if (left.channel != right.channel)
            return left.channel < right.channel;
        return left.reason < right.reason;
    });
    return result;
}

ClientServerIdentity ClientRecoveryState::server_identity() const
{
    std::lock_guard guard(mutex_);
    return server_identity_;
}

std::string ClientRecoveryState::server_epoch() const
{
    std::lock_guard guard(mutex_);
    return server_identity_.server_epoch;
}

const std::string&
ClientRecoveryState::registration_nonce() const noexcept
{
    return registration_nonce_;
}

bool ClientRecoveryState::set_server_identity(
    std::string epoch, std::string connection_token)
{
    std::lock_guard guard(mutex_);
    if (epoch.empty()
        || (epoch == server_identity_.server_epoch
            && connection_token
                == server_identity_.connection_token))
    {
        return false;
    }
    server_identity_ = {
        .server_epoch = std::move(epoch),
        .connection_token = std::move(connection_token),
    };
    return true;
}

bool ClientRecoveryState::set_server_epoch(std::string epoch)
{
    std::lock_guard guard(mutex_);
    if (epoch.empty()
        || epoch == server_identity_.server_epoch)
        return false;
    server_identity_ = {
        .server_epoch = std::move(epoch),
    };
    return true;
}

bool ClientRecoveryState::refresh_server_epoch(
    const std::filesystem::path& runtime_directory,
    std::string_view client_id, std::string& error)
{
    const auto existing = server_identity();
    const auto probe = ServerClient::probe({
        .runtime_directory = runtime_directory,
        .client_id = std::string(client_id),
        .connection_token = existing.connection_token,
        .registration_nonce = registration_nonce_,
        .timeout = std::chrono::seconds(2),
        .request_timeout = std::chrono::milliseconds(500),
        .launch_if_missing = false,
    });
    if (!probe.ready())
    {
        error = probe.error_message.empty()
            ? "The Draxul server is not ready."
            : probe.error_message;
        return false;
    }
    {
        std::lock_guard guard(mutex_);
        if (server_identity_ != existing)
        {
            // Another channel completed a newer refresh while this request
            // was in flight. Its identity is authoritative.
            return true;
        }
        server_identity_ = {
            .server_epoch = probe.welcome->server_epoch,
            .connection_token
                = probe.welcome->connection_token,
        };
    }
    return true;
}

std::chrono::milliseconds ClientRecoveryState::retry_delay_for(
    uint32_t attempt, uint64_t jitter_sample)
{
    constexpr uint64_t base_ms = 100;
    constexpr uint64_t maximum_ms = 5000;
    const uint32_t shift = std::min<uint32_t>(
        attempt > 0 ? attempt - 1 : 0, 6);
    const uint64_t unjittered
        = std::min<uint64_t>(maximum_ms, base_ms << shift);
    // 80%..120%, inclusive. Different client identities seed independent
    // sequences, so multiple UIs do not reconnect in lockstep.
    const uint64_t percent = 80 + jitter_sample % 41;
    return std::chrono::milliseconds(std::clamp<uint64_t>(
        unjittered * percent / 100, 1, maximum_ms));
}

bool is_transient_client_error(std::string_view code)
{
    return code == "endpoint_unavailable"
        || code == "io_error"
        || code == "deadline_exceeded"
        || code == "main_thread_timeout"
        || code == "server_stopping"
        || code == "stale_epoch"
        || code == "not_attached";
}

bool is_resynchronizing_client_error(std::string_view code)
{
    return code == "invalid_connection_token"
        || code == "invalid_event"
        || code == "invalid_poll"
        || code == "invalid_response"
        || code == "stale_sequence"
        || code == "stale_generation"
        || code == "stale_scrollback";
}

std::string_view to_string(ClientConnectionPhase phase)
{
    switch (phase)
    {
    case ClientConnectionPhase::Connected:
        return "connected";
    case ClientConnectionPhase::Degraded:
        return "degraded";
    case ClientConnectionPhase::Reconnecting:
        return "reconnecting";
    }
    return "unknown";
}

std::string_view to_string(ClientRecoveryReasonKind kind)
{
    switch (kind)
    {
    case ClientRecoveryReasonKind::Reconnect:
        return "reconnect";
    case ClientRecoveryReasonKind::Fallback:
        return "fallback";
    case ClientRecoveryReasonKind::Resync:
        return "resync";
    }
    return "unknown";
}

} // namespace draxul
