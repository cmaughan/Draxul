#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace draxul
{

enum class ClientConnectionPhase
{
    Connected,
    Degraded,
    Reconnecting,
};

inline constexpr auto kClientSustainedOutageThreshold
    = std::chrono::seconds(2);
inline constexpr std::size_t kClientRecoveryMaxReasonBuckets = 32;
inline constexpr std::size_t kClientRecoveryMaxChannelBytes = 64;
inline constexpr std::size_t kClientRecoveryMaxReasonBytes = 128;

struct ClientRecoverySnapshot
{
    ClientConnectionPhase phase = ClientConnectionPhase::Connected;
    uint32_t attempts = 0;
    std::chrono::milliseconds retry_delay{ 0 };
    std::string server_epoch;
    std::chrono::milliseconds outage_duration{ 0 };
    bool sustained_outage = false;
    uint64_t interruption_count = 0;
    uint64_t recovery_count = 0;
    std::string current_reason;

    bool operator==(const ClientRecoverySnapshot&) const = default;
};

enum class ClientRecoveryReasonKind
{
    Reconnect,
    Fallback,
    Resync,
};

struct ClientRecoveryReasonCount
{
    ClientRecoveryReasonKind kind = ClientRecoveryReasonKind::Reconnect;
    std::string channel;
    std::string reason;
    uint64_t count = 0;

    bool operator==(const ClientRecoveryReasonCount&) const = default;
};

struct ClientRecoveryMetricsSnapshot
{
    uint64_t reconnect_attempts = 0;
    uint64_t fallbacks = 0;
    uint64_t resyncs = 0;
    uint64_t reason_overflow = 0;
    std::vector<ClientRecoveryReasonCount> reasons;

    bool operator==(const ClientRecoveryMetricsSnapshot&) const = default;
};

struct ClientServerIdentity
{
    std::string server_epoch;
    std::string connection_token;

    bool operator==(const ClientServerIdentity&) const = default;
};

// Shared recovery policy for all client-side server projections. A single
// instance is shared by the Session worker and every terminal host in one UI,
// so epoch migration and retry pacing cannot drift between subsystems.
class ClientRecoveryState
{
public:
    explicit ClientRecoveryState(std::string jitter_identity = {});

    bool note_connected(std::string_view channel);
    std::chrono::milliseconds note_failure(std::string_view channel,
        std::string_view reason = {});
    // Mirrors a physical channel failure into an aggregate outage view without
    // counting a second reconnect attempt or reason event.
    void note_aggregate_failure(std::string_view channel,
        std::string_view reason = {});
    void note_reconnecting(std::string_view channel);
    ClientRecoverySnapshot snapshot(std::string_view channel) const;
    ClientRecoverySnapshot snapshot_at(std::string_view channel,
        std::chrono::steady_clock::time_point now) const;
    void note_fallback(
        std::string_view channel, std::string_view reason);
    void note_resync(
        std::string_view channel, std::string_view reason);
    ClientRecoveryMetricsSnapshot metrics_snapshot() const;

    ClientServerIdentity server_identity() const;
    std::string server_epoch() const;
    const std::string& registration_nonce() const noexcept;
    bool set_server_identity(
        std::string epoch, std::string connection_token);
    bool set_server_epoch(std::string epoch);
    bool refresh_server_epoch(
        const std::filesystem::path& runtime_directory,
        std::string_view client_id, std::string& error);

    // Public for deterministic policy tests. Production calls note_failure().
    static std::chrono::milliseconds retry_delay_for(
        uint32_t attempt, uint64_t jitter_sample);

private:
    struct ChannelState
    {
        ClientConnectionPhase phase = ClientConnectionPhase::Connected;
        uint32_t attempts = 0;
        std::chrono::milliseconds retry_delay{ 0 };
        uint64_t jitter_state = 0;
        std::optional<std::chrono::steady_clock::time_point>
            outage_started_at;
        uint64_t interruption_count = 0;
        uint64_t recovery_count = 0;
        std::string current_reason;
    };

    void record_reason_locked(ClientRecoveryReasonKind kind,
        std::string_view channel, std::string_view reason);
    std::chrono::milliseconds note_failure_locked(
        std::string_view channel, std::string_view reason,
        bool record_metrics);

    ChannelState& channel_locked(std::string_view channel);
    mutable std::mutex mutex_;
    ClientServerIdentity server_identity_;
    std::string registration_nonce_;
    uint64_t jitter_identity_ = 0;
    std::unordered_map<std::string, ChannelState> channels_;
    ClientRecoveryMetricsSnapshot metrics_;
};

std::string_view to_string(ClientConnectionPhase phase);
std::string_view to_string(ClientRecoveryReasonKind kind);

bool is_transient_client_error(std::string_view code);
bool is_resynchronizing_client_error(std::string_view code);

} // namespace draxul
