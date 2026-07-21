#include "session_listing.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace draxul
{

namespace
{

std::string display_name_text(const SessionSummary& session)
{
    if (!session.session_name.empty() && session.session_name != session.session_id)
        return session.session_name;
    return {};
}

} // namespace

std::string session_entry_name(const SessionSummary& session)
{
    if (!session.session_name.empty() && session.session_name != session.session_id)
        return session.session_name + " (" + session.session_id + ")";
    return session.session_id;
}

std::string session_entry_hint(const SessionSummary& session)
{
    return "saved " + std::to_string(session.workspace_count) + "w/"
        + std::to_string(session.pane_count) + "p";
}

std::vector<SessionSummary> list_known_sessions(std::string* error)
{
    return list_saved_sessions(error);
}

std::string format_session_listing_table(const std::vector<SessionSummary>& sessions)
{
    size_t id_width = std::string_view("SESSION ID").size();
    size_t workspace_width = std::string_view("WORKSPACES").size();
    size_t pane_width = std::string_view("PANES").size();

    for (const auto& session : sessions)
        id_width = std::max(id_width, session.session_id.size());

    std::ostringstream out;
    out << std::left << std::setw(static_cast<int>(id_width)) << "SESSION ID"
        << "  " << std::left << std::setw(static_cast<int>(workspace_width)) << "WORKSPACES"
        << "  " << std::left << std::setw(static_cast<int>(pane_width)) << "PANES"
        << "  NAME\n";

    out << std::string(id_width, '-')
        << "  " << std::string(workspace_width, '-')
        << "  " << std::string(pane_width, '-')
        << "  ----\n";

    for (const auto& session : sessions)
    {
        out << std::left << std::setw(static_cast<int>(id_width)) << session.session_id
            << "  " << std::right << std::setw(static_cast<int>(workspace_width)) << session.workspace_count
            << "  " << std::right << std::setw(static_cast<int>(pane_width)) << session.pane_count
            << "  " << display_name_text(session) << '\n';
    }

    return out.str();
}

} // namespace draxul
