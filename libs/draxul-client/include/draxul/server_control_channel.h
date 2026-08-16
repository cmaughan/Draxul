#pragma once

#include <draxul/control_plane.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace draxul
{

class ClientRecoveryState;

struct ServerControlChannelOptions
{
    std::filesystem::path runtime_directory;
    std::string client_id;
    std::string session_id = "default";
    std::shared_ptr<ClientRecoveryState> recovery;
};

// The one control-plane request policy for every client-side channel to the
// shared server: the session/client/connection-token envelope, and the
// fast-attempt -> classify -> epoch-refresh -> single-retry recovery ladder
// previously copy-pasted by App's agent mutations and the polled clients.
class ServerControlChannel
{
public:
    explicit ServerControlChannel(ServerControlChannelOptions options);

    const ServerControlChannelOptions& options() const noexcept;

    // Returns params with the standard envelope merged in: session_id
    // (defaulted to "default"), client_id when known, and the recovery
    // state's current connection token when one is attached.
    nlohmann::json envelope(nlohmann::json params) const;

    // Envelope + one request. `timeout` falls back to the control plane's
    // default request budget.
    ControlClientResult request(std::string_view method,
        nlohmann::json params,
        std::optional<std::chrono::milliseconds> timeout
        = std::nullopt) const;

    // Transport without the envelope, for callers that assemble their own
    // params.
    ControlClientResult raw_request(std::string_view method,
        nlohmann::json params,
        std::optional<std::chrono::milliseconds> timeout
        = std::nullopt) const;

    // Mutation policy: one fast 100ms attempt; when the failure is
    // transient or resynchronizing, refresh the server epoch once if the
    // connection token was rejected (invalid_connection_token /
    // stale_epoch) and retry once with a 500ms budget. The retry rebuilds
    // the envelope, so a refreshed or cleared connection token is applied
    // automatically.
    ControlClientResult request_with_recovery(std::string_view method,
        nlohmann::json params) const;

private:
    ServerControlChannelOptions options_;
};

} // namespace draxul
