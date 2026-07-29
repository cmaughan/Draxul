#include <draxul/server_protocol.h>

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
    status.spaces = value.at("spaces").get<size_t>();
    status.terminals = value.at("terminals").get<size_t>();
    status.live_terminals = value.at("live_terminals").get<size_t>();
    status.checkpoint_path
        = value.value("checkpoint_path", std::string{});
    status.checkpoint_state
        = value.value("checkpoint_state", std::string{});
    status.last_checkpoint_unix_ms
        = value.value("last_checkpoint_unix_ms", uint64_t{ 0 });
    status.checkpoint_error
        = value.value("checkpoint_error", std::string{});
    status.restore_warnings = value.value(
        "restore_warnings", std::vector<std::string>{});
    if (status.session_id.empty()
        || status.session_id.size() > kServerMaxSessionIdBytes
        || std::ranges::any_of(status.session_id,
            [](unsigned char ch) {
                return ch < 0x20 || ch == 0x7f;
            })
        || status.checkpoint_path.size() > 4096
        || status.checkpoint_state.size() > 64
        || status.checkpoint_error.size() > 4096
        || status.restore_warnings.size() > 64
        || std::ranges::any_of(status.restore_warnings,
            [](const std::string& warning) {
                return warning.size() > 4096;
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

nlohmann::json server_hello_to_json(const ServerHello& hello)
{
    return {
        { "protocol_major", hello.protocol_major },
        { "protocol_minor", hello.protocol_minor },
        { "client_id", hello.client_id },
        { "capabilities", hello.capabilities },
    };
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
        || !value.contains("capabilities")
        || !valid_capabilities(value["capabilities"]))
    {
        error = "Server hello is invalid.";
        return std::nullopt;
    }

    ServerHello hello;
    hello.protocol_major = value["protocol_major"].get<int>();
    hello.protocol_minor = value["protocol_minor"].get<int>();
    hello.client_id = value["client_id"].get<std::string>();
    hello.capabilities = read_capabilities(value["capabilities"]);
    if (hello.protocol_major < 0 || hello.protocol_minor < 0
        || hello.client_id.empty()
        || hello.client_id.size() > kServerMaxClientIdBytes)
    {
        error = "Server hello values are out of range.";
        return std::nullopt;
    }
    return hello;
}

nlohmann::json server_welcome_to_json(const ServerWelcome& welcome)
{
    return {
        { "protocol_major", welcome.protocol_major },
        { "protocol_minor", welcome.protocol_minor },
        { "server_pid", welcome.server_pid },
        { "server_epoch", welcome.server_epoch },
        { "build_version", welcome.build_version },
        { "capabilities", welcome.capabilities },
    };
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
        || !value.contains("capabilities")
        || !valid_capabilities(value["capabilities"]))
    {
        error = "Server welcome is invalid.";
        return std::nullopt;
    }

    ServerWelcome welcome;
    welcome.protocol_major = value["protocol_major"].get<int>();
    welcome.protocol_minor = value["protocol_minor"].get<int>();
    welcome.server_pid = value["server_pid"].get<uint64_t>();
    welcome.server_epoch = value["server_epoch"].get<std::string>();
    welcome.build_version = value["build_version"].get<std::string>();
    welcome.capabilities = read_capabilities(value["capabilities"]);
    if (welcome.protocol_major < 0 || welcome.protocol_minor < 0
        || welcome.server_pid == 0 || welcome.server_epoch.empty()
        || welcome.server_epoch.size() > 128
        || welcome.build_version.size() > 128)
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
        status.protocol_major = value.at("protocol_major").get<int>();
        status.protocol_minor = value.at("protocol_minor").get<int>();
        status.server_pid = value.at("server_pid").get<uint64_t>();
        status.server_epoch = value.at("server_epoch").get<std::string>();
        status.build_version = value.at("build_version").get<std::string>();
        status.uptime_ms = value.at("uptime_ms").get<uint64_t>();
        status.connected_clients = value.at("connected_clients").get<size_t>();
        status.sessions = value.at("sessions").get<size_t>();
        status.spaces = value.at("spaces").get<size_t>();
        status.terminals = value.at("terminals").get<size_t>();
        status.agents = value.at("agents").get<size_t>();
        status.checkpoint_path
            = value.value("checkpoint_path", std::string{});
        status.checkpoint_state
            = value.value("checkpoint_state", std::string{});
        status.last_checkpoint_unix_ms
            = value.value("last_checkpoint_unix_ms", uint64_t{ 0 });
        status.checkpoint_error
            = value.value("checkpoint_error", std::string{});
        status.restore_warnings = value.value(
            "restore_warnings", std::vector<std::string>{});
        if (const auto statuses = value.find("session_statuses");
            statuses != value.end())
        {
            if (!statuses->is_array() || statuses->size() > 256)
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
        if (status.state.empty() || status.server_pid == 0
            || status.server_epoch.empty()
            || status.checkpoint_path.size() > 4096
            || status.checkpoint_state.size() > 64
            || status.checkpoint_error.size() > 4096
            || status.restore_warnings.size() > 64
            || std::ranges::any_of(status.restore_warnings,
                [](const std::string& warning) {
                    return warning.size() > 4096;
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
