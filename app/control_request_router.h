#pragma once

#include <draxul/control_plane.h>

#include <string>

namespace draxul
{

class AgentController;
class SpaceController;

// Resolves local control requests on the application thread. The transport
// never receives direct access to Spaces, panes, or hosts.
class ControlRequestRouter
{
public:
    ControlRequestRouter(
        SpaceController& spaces, AgentController& agents, std::string session_id);

    ControlMethodResult handle(const ControlRequest& request);

private:
    SpaceController& spaces_;
    AgentController& agents_;
    std::string session_id_;
};

} // namespace draxul
