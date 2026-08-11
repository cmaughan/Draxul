#pragma once

#include <draxul/session_model.h>
#include <draxul/topology_protocol.h>

#include <optional>
#include <string>
#include <vector>

namespace draxul
{

struct SessionTopologyRestore
{
    TopologySnapshot topology;
    std::vector<std::string> warnings;
};

std::optional<SessionSnapshot> capture_session_topology(
    const TopologySnapshot& topology, std::string& error);
std::optional<SessionTopologyRestore> restore_session_topology(
    const SessionSnapshot& session, std::string& error);

} // namespace draxul
