#include <draxul/topology_client.h>

#include <utility>

namespace draxul
{

TopologyClient::TopologyClient(TopologyClientOptions options)
    : RevisionPolledClient(std::move(options))
{
}

std::optional<TopologySnapshot> TopologyClient::parse_snapshot(
    const nlohmann::json& value, std::string& error)
{
    return topology_snapshot_from_json(value, error);
}

bool TopologyClient::execute(TopologyCommand command,
    TopologyCommandResult& result, std::string& error)
{
    if (command.client_id.empty())
        command.client_id = channel_.options().client_id;
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

} // namespace draxul
