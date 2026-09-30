#include <draxul/server_status_text.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace draxul
{
namespace
{
std::string count_label(
    size_t count, std::string_view singular,
    std::string_view plural)
{
    return std::to_string(count) + " "
        + std::string(count == 1 ? singular : plural);
}
} // namespace

ServerStatusText format_server_status_text(
    const ServerStatusSnapshot& status)
{
    size_t live_terminals = 0;
    for (const auto& session : status.session_statuses)
        live_terminals += session.live_terminals;
    return {
        .state = "Draxul Server - "
            + (status.state.empty()
                    ? std::string("unknown")
                    : status.state),
        .clients_and_terminals
        = count_label(
              status.connected_clients, "client", "clients")
            + " - "
            + count_label(
                live_terminals,
                "live terminal", "live terminals")
            + " / "
            + count_label(
                status.terminals, "terminal", "terminals"),
        .sessions_spaces_and_agents
        = count_label(status.sessions, "Session", "Sessions")
            + " - "
            + count_label(status.spaces, "Space", "Spaces")
            + " - "
            + count_label(status.agents, "Agent", "Agents"),
    };
}

std::string format_server_status_summary(
    const ServerStatusSnapshot& status)
{
    const auto text = format_server_status_text(status);
    return text.state + "; " + text.clients_and_terminals
        + "; " + text.sessions_spaces_and_agents;
}

std::string format_server_session_listing_table(
    const std::vector<ServerSessionStatusSnapshot>& sessions)
{
    size_t id_width = std::string_view("SESSION ID").size();
    size_t name_width = std::string_view("NAME").size();
    const size_t space_width
        = std::string_view("SPACES").size();
    const size_t terminal_width
        = std::string_view("TERMINALS").size();
    const size_t live_width = std::string_view("LIVE").size();
    const size_t checkpoint_width
        = std::string_view("CHECKPOINT").size();

    for (const auto& session : sessions)
    {
        id_width = std::max(id_width, session.session_id.size());
        name_width = std::max(name_width,
            (session.session_name.empty()
                    ? session.session_id
                    : session.session_name)
                .size());
    }

    std::ostringstream out;
    out << std::left
        << std::setw(static_cast<int>(id_width))
        << "SESSION ID"
        << "  " << std::left
        << std::setw(static_cast<int>(name_width))
        << "NAME"
        << "  " << std::left
        << std::setw(static_cast<int>(space_width))
        << "SPACES"
        << "  " << std::left
        << std::setw(static_cast<int>(terminal_width))
        << "TERMINALS"
        << "  " << std::left
        << std::setw(static_cast<int>(live_width))
        << "LIVE"
        << "  CHECKPOINT\n";

    out << std::string(id_width, '-')
        << "  " << std::string(name_width, '-')
        << "  " << std::string(space_width, '-')
        << "  " << std::string(terminal_width, '-')
        << "  " << std::string(live_width, '-')
        << "  " << std::string(checkpoint_width, '-')
        << '\n';

    for (const auto& session : sessions)
    {
        out << std::left
            << std::setw(static_cast<int>(id_width))
            << session.session_id
            << "  " << std::left
            << std::setw(static_cast<int>(name_width))
            << (session.session_name.empty()
                       ? session.session_id
                       : session.session_name)
            << "  " << std::right
            << std::setw(static_cast<int>(space_width))
            << session.spaces
            << "  " << std::right
            << std::setw(static_cast<int>(terminal_width))
            << session.terminals
            << "  " << std::right
            << std::setw(static_cast<int>(live_width))
            << session.live_terminals
            << "  " << session.checkpoint_state << '\n';
    }
    return out.str();
}

} // namespace draxul
