#include <draxul/server_protocol.h>

#include "json_extract.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace draxul
{

namespace
{

bool valid_capabilities(const nlohmann::json& value)
{
    if (!value.is_array() || value.size() > kServerMaxCapabilities)
        return false;
    std::unordered_set<std::string> unique;
    for (const auto& item : value)
    {
        if (!item.is_string())
            return false;
        const std::string capability = item.get<std::string>();
        if (capability.empty() || capability.size() > 64
            || !unique.insert(capability).second)
        {
            return false;
        }
    }
    return true;
}

bool has_control_characters(std::string_view value)
{
    return std::ranges::any_of(value,
        [](unsigned char ch) {
            return ch < 0x20 || ch == 0x7f;
        });
}

std::vector<std::string> read_capabilities(const nlohmann::json& value)
{
    std::vector<std::string> result;
    result.reserve(value.size());
    for (const auto& item : value)
        result.push_back(item.get<std::string>());
    return result;
}

nlohmann::json session_status_to_json(
    const ServerSessionStatusSnapshot& status)
{
    return {
        { "session_id", status.session_id },
        { "session_name", status.session_name },
        { "spaces", status.spaces },
        { "terminals", status.terminals },
        { "live_terminals", status.live_terminals },
        { "checkpoint_path", status.checkpoint_path },
        { "checkpoint_state", status.checkpoint_state },
        { "last_checkpoint_unix_ms",
            status.last_checkpoint_unix_ms },
        { "checkpoint_error", status.checkpoint_error },
        { "restore_warnings", status.restore_warnings },
    };
}

std::optional<ServerSessionStatusSnapshot> session_status_from_json(
    const nlohmann::json& value)
{
    if (!value.is_object())
        return std::nullopt;
    ServerSessionStatusSnapshot status;
    status.session_id = value.at("session_id").get<std::string>();
    status.session_name
        = value.value("session_name", status.session_id);
    if (!read_bounded_integer(value.at("spaces"), status.spaces)
        || !read_bounded_integer(
            value.at("terminals"), status.terminals)
        || !read_bounded_integer(
            value.at("live_terminals"), status.live_terminals))
    {
        return std::nullopt;
    }
    status.checkpoint_path
        = value.value("checkpoint_path", std::string{});
    status.checkpoint_state
        = value.value("checkpoint_state", std::string{});
    if (const auto checkpoint
        = value.find("last_checkpoint_unix_ms");
        checkpoint != value.end()
        && !read_bounded_integer(
            *checkpoint, status.last_checkpoint_unix_ms))
    {
        return std::nullopt;
    }
    status.checkpoint_error
        = value.value("checkpoint_error", std::string{});
    status.restore_warnings = value.value(
        "restore_warnings", std::vector<std::string>{});
    if (status.session_id.empty()
        || status.session_id.size() > kServerMaxSessionIdBytes
        || has_control_characters(status.session_id)
        || status.session_name.size() > kServerMaxSessionIdBytes
        || has_control_characters(status.session_name)
        || status.spaces > kServerMaxStatusAggregateCount
        || status.terminals > kServerMaxStatusAggregateCount
        || status.live_terminals > status.terminals
        || status.checkpoint_path.size()
            > kServerMaxStatusDetailBytes
        || status.checkpoint_state.size() > 64
        || status.checkpoint_error.size()
            > kServerMaxStatusDetailBytes
        || status.restore_warnings.size()
            > kServerMaxRestoreWarnings
        || std::ranges::any_of(status.restore_warnings,
            [](const std::string& warning) {
                return warning.size()
                    > kServerMaxStatusDetailBytes;
            }))
    {
        return std::nullopt;
    }
    return status;
}

} // namespace

std::string_view to_string(ServerProbeState state)
{
    switch (state)
    {
    case ServerProbeState::Absent:
        return "absent";
    case ServerProbeState::Starting:
        return "starting";
    case ServerProbeState::Ready:
        return "ready";
    case ServerProbeState::Busy:
        return "busy";
    case ServerProbeState::Incompatible:
        return "incompatible";
    case ServerProbeState::Crashed:
        return "crashed";
    case ServerProbeState::Stale:
        return "stale";
    case ServerProbeState::LaunchFailed:
        return "launch_failed";
    }
    return "unknown";
}

std::string server_build_version()
{
#ifdef DRAXUL_BUILD_VERSION
    return DRAXUL_BUILD_VERSION;
#else
    return "unknown";
#endif
}

bool valid_server_client_id(std::string_view value)
{
    return !value.empty()
        && value.size() <= kServerMaxClientIdBytes
        && !has_control_characters(value);
}

nlohmann::json server_hello_to_json(const ServerHello& hello)
{
    nlohmann::json result = {
        { "protocol_major", hello.protocol_major },
        { "protocol_minor", hello.protocol_minor },
        { "client_id", hello.client_id },
        { "capabilities", hello.capabilities },
    };
    if (!hello.connection_token.empty())
        result["connection_token"] = hello.connection_token;
    if (!hello.registration_nonce.empty())
        result["registration_nonce"] = hello.registration_nonce;
    return result;
}

std::optional<ServerHello> server_hello_from_json(
    const nlohmann::json& value, std::string& error)
{
    if (!value.is_object()
        || !value.contains("protocol_major")
        || !value["protocol_major"].is_number_integer()
        || !value.contains("protocol_minor")
        || !value["protocol_minor"].is_number_integer()
        || !value.contains("client_id")
        || !value["client_id"].is_string()
        || (value.contains("connection_token")
            && !value["connection_token"].is_string())
        || (value.contains("registration_nonce")
            && !value["registration_nonce"].is_string())
        || !value.contains("capabilities")
        || !valid_capabilities(value["capabilities"]))
    {
        error = "Server hello is invalid.";
        return std::nullopt;
    }

    ServerHello hello;
    if (!read_bounded_integer(
            value["protocol_major"], hello.protocol_major)
        || !read_bounded_integer(
            value["protocol_minor"], hello.protocol_minor))
    {
        error = "Server hello values are out of range.";
        return std::nullopt;
    }
    hello.client_id = value["client_id"].get<std::string>();
    hello.connection_token
        = value.value("connection_token", std::string{});
    hello.registration_nonce
        = value.value("registration_nonce", std::string{});
    hello.capabilities = read_capabilities(value["capabilities"]);
    if (hello.protocol_major < 0 || hello.protocol_minor < 0
        || !valid_server_client_id(hello.client_id)
        || hello.connection_token.size()
            > kServerMaxConnectionTokenBytes
        || has_control_characters(hello.connection_token)
        || hello.registration_nonce.size()
            > kServerMaxConnectionTokenBytes
        || has_control_characters(hello.registration_nonce))
    {
        error = "Server hello values are out of range.";
        return std::nullopt;
    }
    return hello;
}

nlohmann::json server_welcome_to_json(const ServerWelcome& welcome)
{
    nlohmann::json result = {
        { "protocol_major", welcome.protocol_major },
        { "protocol_minor", welcome.protocol_minor },
        { "server_pid", welcome.server_pid },
        { "server_epoch", welcome.server_epoch },
        { "build_version", welcome.build_version },
        { "capabilities", welcome.capabilities },
    };
    if (!welcome.connection_token.empty())
        result["connection_token"] = welcome.connection_token;
    return result;
}

std::optional<ServerWelcome> server_welcome_from_json(
    const nlohmann::json& value, std::string& error)
{
    if (!value.is_object()
        || !value.contains("protocol_major")
        || !value["protocol_major"].is_number_integer()
        || !value.contains("protocol_minor")
        || !value["protocol_minor"].is_number_integer()
        || !value.contains("server_pid")
        || !value["server_pid"].is_number_unsigned()
        || !value.contains("server_epoch")
        || !value["server_epoch"].is_string()
        || !value.contains("build_version")
        || !value["build_version"].is_string()
        || (value.contains("connection_token")
            && !value["connection_token"].is_string())
        || !value.contains("capabilities")
        || !valid_capabilities(value["capabilities"]))
    {
        error = "Server welcome is invalid.";
        return std::nullopt;
    }

    ServerWelcome welcome;
    if (!read_bounded_integer(
            value["protocol_major"], welcome.protocol_major)
        || !read_bounded_integer(
            value["protocol_minor"], welcome.protocol_minor)
        || !read_bounded_integer(
            value["server_pid"], welcome.server_pid))
    {
        error = "Server welcome values are out of range.";
        return std::nullopt;
    }
    welcome.server_epoch = value["server_epoch"].get<std::string>();
    welcome.build_version = value["build_version"].get<std::string>();
    welcome.connection_token
        = value.value("connection_token", std::string{});
    welcome.capabilities = read_capabilities(value["capabilities"]);
    if (welcome.protocol_major < 0 || welcome.protocol_minor < 0
        || welcome.server_pid == 0 || welcome.server_epoch.empty()
        || welcome.server_epoch.size()
            > kServerMaxHandshakeTextBytes
        || welcome.build_version.size()
            > kServerMaxHandshakeTextBytes
        || welcome.connection_token.size()
            > kServerMaxConnectionTokenBytes
        || has_control_characters(welcome.connection_token))
    {
        error = "Server welcome values are out of range.";
        return std::nullopt;
    }
    return welcome;
}

nlohmann::json server_status_to_json(const ServerStatusSnapshot& status)
{
    nlohmann::json session_statuses = nlohmann::json::array();
    for (const auto& session : status.session_statuses)
        session_statuses.push_back(session_status_to_json(session));
    return {
        { "state", status.state },
        { "protocol_major", status.protocol_major },
        { "protocol_minor", status.protocol_minor },
        { "server_pid", status.server_pid },
        { "server_epoch", status.server_epoch },
        { "build_version", status.build_version },
        { "uptime_ms", status.uptime_ms },
        { "connected_clients", status.connected_clients },
        { "sessions", status.sessions },
        { "spaces", status.spaces },
        { "terminals", status.terminals },
        { "agents", status.agents },
        { "scrollback_cells_reserved",
            status.scrollback_cells_reserved },
        { "scrollback_cells_limit",
            status.scrollback_cells_limit },
        { "checkpoint_path", status.checkpoint_path },
        { "checkpoint_state", status.checkpoint_state },
        { "last_checkpoint_unix_ms",
            status.last_checkpoint_unix_ms },
        { "checkpoint_error", status.checkpoint_error },
        { "restore_warnings", status.restore_warnings },
        { "session_statuses", std::move(session_statuses) },
    };
}

std::optional<ServerStatusSnapshot> server_status_from_json(
    const nlohmann::json& value, std::string& error)
{
    if (!value.is_object())
    {
        error = "Server status is invalid.";
        return std::nullopt;
    }
    try
    {
        ServerStatusSnapshot status;
        status.state = value.at("state").get<std::string>();
        status.server_epoch = value.at("server_epoch").get<std::string>();
        status.build_version = value.at("build_version").get<std::string>();
        if (!read_bounded_integer(
                value.at("protocol_major"), status.protocol_major)
            || !read_bounded_integer(
                value.at("protocol_minor"), status.protocol_minor)
            || !read_bounded_integer(
                value.at("server_pid"), status.server_pid)
            || !read_bounded_integer(
                value.at("uptime_ms"), status.uptime_ms)
            || !read_bounded_integer(
                value.at("connected_clients"),
                status.connected_clients)
            || !read_bounded_integer(
                value.at("sessions"), status.sessions)
            || !read_bounded_integer(
                value.at("spaces"), status.spaces)
            || !read_bounded_integer(
                value.at("terminals"), status.terminals)
            || !read_bounded_integer(
                value.at("agents"), status.agents))
        {
            error = "Server status values are out of range.";
            return std::nullopt;
        }
        status.checkpoint_path
            = value.value("checkpoint_path", std::string{});
        if (const auto reserved
            = value.find("scrollback_cells_reserved");
            reserved != value.end()
            && !read_bounded_integer(
                *reserved, status.scrollback_cells_reserved))
        {
            error = "Server status scrollback reservation is invalid.";
            return std::nullopt;
        }
        if (const auto limit
            = value.find("scrollback_cells_limit");
            limit != value.end()
            && !read_bounded_integer(
                *limit, status.scrollback_cells_limit))
        {
            error = "Server status scrollback limit is invalid.";
            return std::nullopt;
        }
        status.checkpoint_state
            = value.value("checkpoint_state", std::string{});
        if (const auto checkpoint
            = value.find("last_checkpoint_unix_ms");
            checkpoint != value.end()
            && !read_bounded_integer(
                *checkpoint, status.last_checkpoint_unix_ms))
        {
            error = "Server status checkpoint timestamp is invalid.";
            return std::nullopt;
        }
        status.checkpoint_error
            = value.value("checkpoint_error", std::string{});
        status.restore_warnings = value.value(
            "restore_warnings", std::vector<std::string>{});
        if (const auto statuses = value.find("session_statuses");
            statuses != value.end())
        {
            if (!statuses->is_array()
                || statuses->size() > kServerMaxSessions)
            {
                error = "Server Session status list is invalid.";
                return std::nullopt;
            }
            status.session_statuses.reserve(statuses->size());
            for (const auto& item : *statuses)
            {
                auto parsed = session_status_from_json(item);
                if (!parsed)
                {
                    error = "Server Session status is invalid.";
                    return std::nullopt;
                }
                status.session_statuses.push_back(
                    std::move(*parsed));
            }
        }
        if (status.state.empty()
            || status.state.size() > kServerMaxStatusStateBytes
            || status.protocol_major < 0
            || status.protocol_minor < 0
            || status.server_pid == 0
            || status.server_epoch.empty()
            || status.server_epoch.size()
                > kServerMaxHandshakeTextBytes
            || status.build_version.size()
                > kServerMaxHandshakeTextBytes
            || status.connected_clients
                > kServerMaxConnectedClients
            || status.sessions > kServerMaxSessions
            || status.spaces
                > kServerMaxStatusAggregateCount
            || status.terminals
                > kServerMaxStatusAggregateCount
            || status.agents
                > kServerMaxStatusAggregateCount
            || status.scrollback_cells_reserved
                > kServerMaxStatusResourceCells
            || status.scrollback_cells_limit
                > kServerMaxStatusResourceCells
            || (status.scrollback_cells_limit != 0
                && status.scrollback_cells_reserved
                    > status.scrollback_cells_limit)
            || status.checkpoint_path.size()
                > kServerMaxStatusDetailBytes
            || status.checkpoint_state.size() > 64
            || status.checkpoint_error.size()
                > kServerMaxStatusDetailBytes
            || status.restore_warnings.size()
                > kServerMaxRestoreWarnings
            || std::ranges::any_of(status.restore_warnings,
                [](const std::string& warning) {
                    return warning.size()
                        > kServerMaxStatusDetailBytes;
                }))
        {
            error = "Server status values are out of range.";
            return std::nullopt;
        }
        return status;
    }
    catch (const nlohmann::json::exception&)
    {
        error = "Server status is invalid.";
        return std::nullopt;
    }
}

} // namespace draxul
