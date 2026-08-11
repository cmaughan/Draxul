#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace draxul
{

enum class ClientConnectionPhase
{
    Connected,
    Degraded,
    Reconnecting,
};

struct ClientRecoverySnapshot
{
    ClientConnectionPhase phase = ClientConnectionPhase::Connected;
    uint32_t attempts = 0;
    std::chrono::milliseconds retry_delay{ 0 };
    std::string server_epoch;
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

    void note_connected(std::string_view channel);
    std::chrono::milliseconds note_failure(std::string_view channel);
    void note_reconnecting(std::string_view channel);
    ClientRecoverySnapshot snapshot(std::string_view channel) const;

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
    };

    ChannelState& channel_locked(std::string_view channel);
    mutable std::mutex mutex_;
    ClientServerIdentity server_identity_;
    std::string registration_nonce_;
    uint64_t jitter_identity_ = 0;
    std::unordered_map<std::string, ChannelState> channels_;
};

bool is_transient_client_error(std::string_view code);
bool is_resynchronizing_client_error(std::string_view code);

} // namespace draxul
