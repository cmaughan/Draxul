#pragma once

#include <draxul/agent_protocol.h>
#include <draxul/revision_polled_client.h>

namespace draxul
{

using AgentClientOptions = ServerControlChannelOptions;

class AgentClient
    : public RevisionPolledClient<AgentClient, ServerAgentSnapshot>
{
public:
    explicit AgentClient(AgentClientOptions options);

    static constexpr std::string_view kMethodPrefix = "agent";
    static constexpr std::string_view kStaleRevisionErrorCode
        = "stale_agent_revision";
    static std::optional<ServerAgentSnapshot> parse_snapshot(
        const nlohmann::json& value, std::string& error);
};

} // namespace draxul
