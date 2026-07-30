#include <draxul/topology_client.h>

#include <draxul/client_recovery.h>
#include <draxul/control_plane.h>
#include <draxul/server_client.h>

#include <nlohmann/json.hpp>

namespace draxul
{

TopologyClient::TopologyClient(TopologyClientOptions options)
    : options_(std::move(options))
{
}

bool TopologyClient::refresh(std::string& error)
{
    nlohmann::json result;
    if (!request("topology.snapshot", nlohmann::json::object(),
            result, error))
    {
        return false;
    }
    auto parsed = topology_snapshot_from_json(result, error);
    if (!parsed)
    {
        last_error_code_ = "invalid_response";
        return false;
    }
    snapshot_ = std::move(*parsed);
    return true;
}

bool TopologyClient::poll(bool& changed, std::string& error)
{
    changed = false;
    nlohmann::json result;
    if (!request("topology.poll",
            { { "after_revision", snapshot_.revision } },
            result, error))
    {
        if (last_error_code_ == "stale_topology_revision")
        {
            if (!refresh(error))
                return false;
            changed = true;
            return true;
        }
        return false;
    }
    if (!result.is_object()
        || !result.contains("changed")
        || !result["changed"].is_boolean()
        || !result.contains("revision")
        || !result["revision"].is_number_unsigned())
    {
        last_error_code_ = "invalid_response";
        error = "Invalid topology poll result.";
        return false;
    }
    changed = result["changed"].get<bool>();
    if (!changed)
        return true;
    if (!result.contains("snapshot"))
    {
        last_error_code_ = "invalid_response";
        error = "Changed topology poll has no snapshot.";
        return false;
    }
    auto parsed
        = topology_snapshot_from_json(result["snapshot"], error);
    if (!parsed)
    {
        last_error_code_ = "invalid_response";
        return false;
    }
    snapshot_ = std::move(*parsed);
    return true;
}

bool TopologyClient::execute(TopologyCommand command,
    TopologyCommandResult& result, std::string& error)
{
    if (command.client_id.empty())
        command.client_id = options_.client_id;
    nlohmann::json response;
    if (!request("topology.command",
            topology_command_to_json(command), response, error))
    {
        return false;
    }
    auto parsed = topology_command_result_from_json(response, error);
    if (!parsed)
    {
        last_error_code_ = "invalid_response";
        return false;
    }
    result = std::move(*parsed);
    snapshot_ = result.snapshot;
    return true;
}

bool TopologyClient::request(std::string_view method,
    nlohmann::json params, nlohmann::json& result,
    std::string& error)
{
    params["session_id"] = options_.session_id.empty()
        ? "default"
        : options_.session_id;
    if (!options_.client_id.empty())
    {
        params["client_id"] = options_.client_id;
        if (options_.recovery)
        {
            const auto identity
                = options_.recovery->server_identity();
            if (!identity.connection_token.empty())
            {
                params["connection_token"]
                    = identity.connection_token;
            }
        }
    }
    const auto response = ControlClient::request(
        namespaced_control_id(
            kServerControlId, options_.runtime_directory),
        options_.runtime_directory, method, std::move(params));
    if (!response.ok)
    {
        last_error_code_ = response.error_code;
        error = response.error_message;
        return false;
    }
    last_error_code_.clear();
    result = response.result;
    return true;
}

} // namespace draxul
