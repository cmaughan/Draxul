#include <draxul/session_protocol.h>

#include <nlohmann/json.hpp>
#include <unordered_set>

namespace draxul
{

namespace
{

nlohmann::json channel_error(
    std::string_view code, std::string_view message)
{
    if (code.empty())
        return nullptr;
    return { { "code", code }, { "message", message } };
}

bool read_channel_error(const nlohmann::json& value,
    std::string& code, std::string& message)
{
    if (!value.contains("error"))
        return true;
    const auto& error = value["error"];
    if (!error.is_object() || !error.contains("code")
        || !error["code"].is_string()
        || !error.contains("message")
        || !error["message"].is_string())
    {
        return false;
    }
    code = error["code"].get<std::string>();
    message = error["message"].get<std::string>();
    return !code.empty();
}

} // namespace

nlohmann::json session_poll_request_to_json(
    const SessionPollRequest& request)
{
    nlohmann::json terminals = nlohmann::json::array();
    for (const auto& subscription : request.terminals)
    {
        nlohmann::json encoded{
            { "subscription_id", subscription.subscription_id },
            { "terminal_id", subscription.terminal_id },
            { "visibility_generation",
                subscription.visibility_generation },
            { "visible", subscription.visible },
        };
        if (subscription.cursor)
        {
            encoded["cursor"] = {
                { "generation", subscription.cursor->generation },
                { "after_sequence",
                    subscription.cursor->after_sequence },
            };
        }
        else
            encoded["cursor"] = nullptr;
        terminals.push_back(std::move(encoded));
    }
    return {
        { "request_serial", request.request_serial },
        { "server_epoch", request.server_epoch },
        { "topology_after_revision",
            request.topology_after_revision },
        { "agent_after_revision", request.agent_after_revision },
        { "terminals", std::move(terminals) },
    };
}

std::optional<SessionPollRequest> session_poll_request_from_json(
    const nlohmann::json& value, std::string& error)
{
    if (!value.is_object() || !value.contains("request_serial")
        || !value["request_serial"].is_number_unsigned()
        || !value.contains("server_epoch")
        || !value["server_epoch"].is_string()
        || value["server_epoch"].get_ref<const std::string&>().empty()
        || !value.contains("topology_after_revision")
        || !value["topology_after_revision"].is_number_unsigned()
        || !value.contains("agent_after_revision")
        || !value["agent_after_revision"].is_number_unsigned()
        || !value.contains("terminals")
        || !value["terminals"].is_array()
        || value["terminals"].size()
            > kSessionPollMaxSubscriptions)
    {
        error = "Session poll request is invalid or exceeds its subscription limit.";
        return std::nullopt;
    }
    SessionPollRequest result{
        .request_serial = value["request_serial"].get<uint64_t>(),
        .server_epoch = value["server_epoch"].get<std::string>(),
        .topology_after_revision
        = value["topology_after_revision"].get<uint64_t>(),
        .agent_after_revision
        = value["agent_after_revision"].get<uint64_t>(),
    };
    if (result.request_serial == 0)
    {
        error = "Session poll request serial must be non-zero.";
        return std::nullopt;
    }
    std::unordered_set<uint64_t> subscription_ids;
    for (const auto& encoded : value["terminals"])
    {
        if (!encoded.is_object()
            || !encoded.contains("subscription_id")
            || !encoded["subscription_id"].is_number_unsigned()
            || !encoded.contains("terminal_id")
            || !encoded["terminal_id"].is_string()
            || !encoded.contains("visibility_generation")
            || !encoded["visibility_generation"].is_number_unsigned()
            || !encoded.contains("visible")
            || !encoded["visible"].is_boolean())
        {
            error = "Session terminal subscription is invalid.";
            return std::nullopt;
        }
        SessionTerminalSubscription subscription{
            .subscription_id
            = encoded["subscription_id"].get<uint64_t>(),
            .terminal_id = encoded["terminal_id"].get<std::string>(),
            .visibility_generation
            = encoded["visibility_generation"].get<uint64_t>(),
            .visible = encoded["visible"].get<bool>(),
        };
        if (subscription.subscription_id == 0
            || subscription.visibility_generation == 0
            || subscription.terminal_id.empty()
            || subscription.terminal_id.size()
                > kSessionPollMaxTerminalIdBytes
            || !subscription_ids.insert(
                    subscription.subscription_id)
                    .second)
        {
            error = "Session terminal subscriptions require unique bounded identities.";
            return std::nullopt;
        }
        if (encoded.contains("cursor") && !encoded["cursor"].is_null())
        {
            const auto& cursor = encoded["cursor"];
            if (!cursor.is_object()
                || !cursor.contains("generation")
                || !cursor["generation"].is_number_unsigned()
                || !cursor.contains("after_sequence")
                || !cursor["after_sequence"].is_number_unsigned())
            {
                error = "Session terminal cursor is invalid.";
                return std::nullopt;
            }
            subscription.cursor = SessionTerminalCursor{
                .generation = cursor["generation"].get<uint64_t>(),
                .after_sequence
                = cursor["after_sequence"].get<uint64_t>(),
            };
        }
        result.terminals.push_back(std::move(subscription));
    }
    error.clear();
    return result;
}

nlohmann::json session_poll_response_to_json(
    const SessionPollResponse& response)
{
    nlohmann::json topology{
        { "revision", response.topology.revision },
        { "resync", response.topology.resync },
        { "deferred", response.topology.deferred },
    };
    if (response.topology.snapshot)
    {
        topology["snapshot"]
            = topology_snapshot_to_json(*response.topology.snapshot);
    }
    if (!response.topology.error_code.empty())
    {
        topology["error"] = channel_error(
            response.topology.error_code,
            response.topology.error_message);
    }
    nlohmann::json agents{
        { "revision", response.agents.revision },
        { "resync", response.agents.resync },
        { "deferred", response.agents.deferred },
    };
    if (response.agents.snapshot)
    {
        agents["snapshot"]
            = server_agent_snapshot_to_json(*response.agents.snapshot);
    }
    if (!response.agents.error_code.empty())
    {
        agents["error"] = channel_error(
            response.agents.error_code,
            response.agents.error_message);
    }
    nlohmann::json terminals = nlohmann::json::array();
    for (const auto& batch : response.terminals)
    {
        nlohmann::json encoded{
            { "subscription_id", batch.subscription_id },
            { "terminal_id", batch.terminal_id },
            { "visibility_generation",
                batch.visibility_generation },
            { "suspended", batch.suspended },
            { "resync", batch.resync },
            { "more", batch.more },
        };
        if (batch.attach)
            encoded["attach"] = remote_terminal_attach_to_json(*batch.attach);
        if (!batch.events.empty())
        {
            encoded["events"] = nlohmann::json::array();
            for (const auto& event : batch.events)
            {
                encoded["events"].push_back(
                    remote_terminal_event_to_json(event));
            }
        }
        if (!batch.error_code.empty())
        {
            encoded["error"] = channel_error(
                batch.error_code, batch.error_message);
        }
        terminals.push_back(std::move(encoded));
    }
    return {
        { "request_serial", response.request_serial },
        { "server_epoch", response.server_epoch },
        { "topology", std::move(topology) },
        { "agents", std::move(agents) },
        { "terminals", std::move(terminals) },
        { "more", response.more },
    };
}

std::optional<SessionPollResponse> session_poll_response_from_json(
    const nlohmann::json& value, std::string& error)
{
    if (!value.is_object() || !value.contains("request_serial")
        || !value["request_serial"].is_number_unsigned()
        || !value.contains("server_epoch")
        || !value["server_epoch"].is_string()
        || value["server_epoch"].get_ref<const std::string&>().empty()
        || !value.contains("topology")
        || !value["topology"].is_object()
        || !value.contains("agents")
        || !value["agents"].is_object()
        || !value.contains("terminals")
        || !value["terminals"].is_array()
        || value["terminals"].size()
            > kSessionPollMaxSubscriptions
        || !value.contains("more") || !value["more"].is_boolean())
    {
        error = "Session poll response is invalid.";
        return std::nullopt;
    }
    SessionPollResponse result{
        .request_serial = value["request_serial"].get<uint64_t>(),
        .server_epoch = value["server_epoch"].get<std::string>(),
        .more = value["more"].get<bool>(),
    };
    if (result.request_serial == 0)
    {
        error = "Session poll response serial must be non-zero.";
        return std::nullopt;
    }
    const auto parse_snapshot_channel
        = [&](const nlohmann::json& channel, uint64_t& revision,
              bool& resync, bool& deferred,
              std::string& error_code,
              std::string& error_message) {
              if (!channel.contains("revision")
                  || !channel["revision"].is_number_unsigned()
                  || !channel.contains("resync")
                  || !channel["resync"].is_boolean()
                  || !channel.contains("deferred")
                  || !channel["deferred"].is_boolean()
                  || !read_channel_error(channel, error_code,
                      error_message))
              {
                  return false;
              }
              revision = channel["revision"].get<uint64_t>();
              resync = channel["resync"].get<bool>();
              deferred = channel["deferred"].get<bool>();
              return true;
          };
    const auto& topology = value["topology"];
    if (!parse_snapshot_channel(topology,
            result.topology.revision, result.topology.resync,
            result.topology.deferred, result.topology.error_code,
            result.topology.error_message))
    {
        error = "Session topology poll channel is invalid.";
        return std::nullopt;
    }
    if (topology.contains("snapshot"))
    {
        auto parsed = topology_snapshot_from_json(
            topology["snapshot"], error);
        if (!parsed)
            return std::nullopt;
        result.topology.snapshot = std::move(*parsed);
    }
    const auto& agents = value["agents"];
    if (!parse_snapshot_channel(agents,
            result.agents.revision, result.agents.resync,
            result.agents.deferred, result.agents.error_code,
            result.agents.error_message))
    {
        error = "Session agent poll channel is invalid.";
        return std::nullopt;
    }
    if (agents.contains("snapshot"))
    {
        auto parsed = server_agent_snapshot_from_json(
            agents["snapshot"], error);
        if (!parsed)
            return std::nullopt;
        result.agents.snapshot = std::move(*parsed);
    }
    std::unordered_set<uint64_t> subscription_ids;
    for (const auto& encoded : value["terminals"])
    {
        if (!encoded.is_object()
            || !encoded.contains("subscription_id")
            || !encoded["subscription_id"].is_number_unsigned()
            || !encoded.contains("terminal_id")
            || !encoded["terminal_id"].is_string()
            || !encoded.contains("visibility_generation")
            || !encoded["visibility_generation"].is_number_unsigned()
            || !encoded.contains("suspended")
            || !encoded["suspended"].is_boolean()
            || !encoded.contains("resync")
            || !encoded["resync"].is_boolean()
            || !encoded.contains("more")
            || !encoded["more"].is_boolean())
        {
            error = "Session terminal poll batch is invalid.";
            return std::nullopt;
        }
        SessionTerminalPollBatch batch{
            .subscription_id
            = encoded["subscription_id"].get<uint64_t>(),
            .terminal_id = encoded["terminal_id"].get<std::string>(),
            .visibility_generation
            = encoded["visibility_generation"].get<uint64_t>(),
            .suspended = encoded["suspended"].get<bool>(),
            .resync = encoded["resync"].get<bool>(),
            .more = encoded["more"].get<bool>(),
        };
        if (batch.subscription_id == 0
            || batch.visibility_generation == 0
            || batch.terminal_id.empty()
            || batch.terminal_id.size()
                > kSessionPollMaxTerminalIdBytes
            || !subscription_ids.insert(batch.subscription_id).second
            || !read_channel_error(encoded, batch.error_code,
                batch.error_message))
        {
            error = "Session terminal poll batch identity is invalid.";
            return std::nullopt;
        }
        if (encoded.contains("attach"))
        {
            auto parsed = remote_terminal_attach_from_json(
                encoded["attach"], error);
            if (!parsed)
                return std::nullopt;
            batch.attach = std::move(*parsed);
        }
        if (encoded.contains("events"))
        {
            if (!encoded["events"].is_array()
                || encoded["events"].size()
                    > kRemoteTerminalMaxEventsPerPoll)
            {
                error = "Session terminal event batch is invalid.";
                return std::nullopt;
            }
            for (const auto& event : encoded["events"])
            {
                auto parsed = remote_terminal_event_from_json(
                    event, error);
                if (!parsed)
                    return std::nullopt;
                batch.events.push_back(std::move(*parsed));
            }
        }
        result.terminals.push_back(std::move(batch));
    }
    error.clear();
    return result;
}

} // namespace draxul
