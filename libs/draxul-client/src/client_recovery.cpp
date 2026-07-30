#include <draxul/client_recovery.h>

#include <draxul/server_client.h>

#include <algorithm>
#include <functional>

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

void ClientRecoveryState::note_connected(std::string_view channel)
{
    std::lock_guard guard(mutex_);
    auto& state = channel_locked(channel);
    state.phase = ClientConnectionPhase::Connected;
    state.attempts = 0;
    state.retry_delay = {};
}

std::chrono::milliseconds ClientRecoveryState::note_failure(
    std::string_view channel)
{
    std::lock_guard guard(mutex_);
    auto& state = channel_locked(channel);
    state.attempts = std::min<uint32_t>(state.attempts + 1, 64);
    state.phase = state.attempts == 1
        ? ClientConnectionPhase::Degraded
        : ClientConnectionPhase::Reconnecting;
    state.retry_delay = retry_delay_for(
        state.attempts, next_jitter(state.jitter_state));
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
    std::lock_guard guard(mutex_);
    const auto found = channels_.find(std::string(channel));
    const ChannelState empty;
    const auto& state = found == channels_.end()
        ? empty
        : found->second;
    return {
        .phase = state.phase,
        .attempts = state.attempts,
        .retry_delay = state.retry_delay,
        .server_epoch = server_identity_.server_epoch,
    };
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

} // namespace draxul
