#pragma once

#include <draxul/revision_polled_client.h>
#include <draxul/topology_protocol.h>

namespace draxul
{

using TopologyClientOptions = ServerControlChannelOptions;

class TopologyClient
    : public RevisionPolledClient<TopologyClient, TopologySnapshot>
{
public:
    explicit TopologyClient(TopologyClientOptions options);

    bool execute(TopologyCommand command,
        TopologyCommandResult& result, std::string& error);

    static constexpr std::string_view kMethodPrefix = "topology";
    static constexpr std::string_view kStaleRevisionErrorCode
        = "stale_topology_revision";
    static std::optional<TopologySnapshot> parse_snapshot(
        const nlohmann::json& value, std::string& error);
};

} // namespace draxul
