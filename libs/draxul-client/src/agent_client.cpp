#include <draxul/agent_client.h>

#include <utility>

namespace draxul
{

AgentClient::AgentClient(AgentClientOptions options)
    : RevisionPolledClient(std::move(options))
{
}

std::optional<ServerAgentSnapshot> AgentClient::parse_snapshot(
    const nlohmann::json& value, std::string& error)
{
    return server_agent_snapshot_from_json(value, error);
}

} // namespace draxul
