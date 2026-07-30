#include "session_listing.h"

namespace draxul
{

std::string session_entry_name(const SessionSummary& session)
{
    if (!session.session_name.empty() && session.session_name != session.session_id)
        return session.session_name + " (" + session.session_id + ")";
    return session.session_id;
}

std::string session_entry_hint(const SessionSummary& session)
{
    return "saved " + std::to_string(session.space_count) + "s/"
        + std::to_string(session.tab_count) + "t/"
        + std::to_string(session.pane_count) + "p";
}

std::vector<SessionSummary> list_known_sessions(std::string* error)
{
    return list_saved_sessions(error);
}

} // namespace draxul
