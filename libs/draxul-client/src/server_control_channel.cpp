#include <draxul/server_control_channel.h>

#include <draxul/client_recovery.h>
#include <draxul/server_protocol.h>

#include <nlohmann/json.hpp>
#include <utility>

namespace draxul
{

namespace
{

constexpr std::chrono::milliseconds kFastAttemptTimeout{ 100 };
constexpr std::chrono::milliseconds kRecoveryRetryTimeout{ 500 };

} // namespace

ServerControlChannel::ServerControlChannel(
    ServerControlChannelOptions options)
    : options_(std::move(options))
{
}

const ServerControlChannelOptions&
ServerControlChannel::options() const noexcept
{
    return options_;
}

nlohmann::json ServerControlChannel::envelope(
    nlohmann::json params) const
{
    params["session_id"] = options_.session_id.empty()
        ? "default"
        : options_.session_id;
    if (!options_.client_id.empty())
    {
        params["client_id"] = options_.client_id;
        if (options_.recovery)
        {
            const auto identity = options_.recovery->server_identity();
            if (!identity.connection_token.empty())
            {
                params["connection_token"]
                    = identity.connection_token;
            }
        }
    }
    return params;
}

ControlClientResult ServerControlChannel::raw_request(
    std::string_view method, nlohmann::json params,
    std::optional<std::chrono::milliseconds> timeout) const
{
    ControlRequestOptions request_options;
    if (timeout)
        request_options.timeout = *timeout;
    return ControlClient::request(
        namespaced_control_id(
            kServerControlId, options_.runtime_directory),
        options_.runtime_directory, method, std::move(params),
        request_options);
}

ControlClientResult ServerControlChannel::request(
    std::string_view method, nlohmann::json params,
    std::optional<std::chrono::milliseconds> timeout) const
{
    return raw_request(method, envelope(std::move(params)), timeout);
}

ControlClientResult ServerControlChannel::request_with_recovery(
    std::string_view method, nlohmann::json params) const
{
    auto response = request(method, params, kFastAttemptTimeout);
    if (response.ok
        || (!is_transient_client_error(response.error_code)
            && !is_resynchronizing_client_error(response.error_code)))
    {
        return response;
    }
    if (options_.recovery
        && (response.error_code == "invalid_connection_token"
            || response.error_code == "stale_epoch"))
    {
        // A successful refresh updates (or clears) the recovery state's
        // connection token; the retry below rebuilds the envelope with it.
        // A failed refresh keeps the retry's own result as the caller's
        // outcome, matching the previous inline copies.
        std::string refresh_error;
        options_.recovery->refresh_server_epoch(
            options_.runtime_directory, options_.client_id,
            refresh_error);
    }
    return request(method, std::move(params), kRecoveryRetryTimeout);
}

} // namespace draxul
