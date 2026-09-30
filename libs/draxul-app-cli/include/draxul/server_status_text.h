#pragma once

#include <draxul/server_protocol.h>

namespace draxul
{

struct ServerStatusText
{
    std::string state;
    std::string clients_and_terminals;
    std::string sessions_spaces_and_agents;
};

ServerStatusText format_server_status_text(
    const ServerStatusSnapshot& status);
std::string format_server_status_summary(
    const ServerStatusSnapshot& status);
std::string format_server_session_listing_table(
    const std::vector<ServerSessionStatusSnapshot>& sessions);

} // namespace draxul
