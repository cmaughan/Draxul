#include "session_listing.h"

#include <draxul/log.h>
#include <draxul/session_attach.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace draxul
{

namespace
{

const char* session_state_text(const SessionSummary& session)
{
    if (session.live)
        return session.detached ? "detached" : "live";
    if (!session.has_saved_state)
        return "live?";
    return "saved";
}

std::string display_name_text(const SessionSummary& session)
{
    if (!session.session_name.empty() && session.session_name != session.session_id)
        return session.session_name;
    return {};
}

void scrub_stale_runtime_metadata(const SessionSummary& session)
{
    if (!session.live && !session.detached && session.owner_pid == 0)
        return;

    std::string scrub_error;
    if (!clear_session_runtime_liveness(session.session_id, &scrub_error) && !scrub_error.empty())
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to scrub stale session metadata for %s: %s",
            session.session_id.c_str(),
            scrub_error.c_str());
    }
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
    return std::string(session_state_text(session)) + " " + std::to_string(session.workspace_count) + "w/"
        + std::to_string(session.pane_count) + "p";
}

std::vector<SessionSummary> list_known_sessions(std::string* error)
{
    std::string list_error;
    auto sessions = list_saved_sessions(&list_error);
    if (!list_error.empty())
    {
        if (error)
            *error = list_error;
        return {};
    }

    for (auto& session : sessions)
    {
        const auto probe_status = SessionAttachServer::probe(session.session_id);
        if (probe_status == SessionAttachServer::ProbeStatus::Running)
        {
            session.live = true;
            SessionAttachServer::LiveSessionInfo live_info;
            if (SessionAttachServer::query_live_session(session.session_id, &live_info))
            {
                session.detached = live_info.detached;
                session.workspace_count = live_info.workspace_count;
                session.pane_count = live_info.pane_count;
                session.owner_pid = live_info.owner_pid;
                session.last_attached_unix_s = live_info.last_attached_unix_s;
                session.last_detached_unix_s = live_info.last_detached_unix_s;
            }
        }
        else if (probe_status == SessionAttachServer::ProbeStatus::NoServer)
        {
            scrub_stale_runtime_metadata(session);
            session.live = false;
            session.detached = false;
            session.owner_pid = 0;
        }
    }

    sessions.erase(std::remove_if(sessions.begin(), sessions.end(), [](const SessionSummary& session) {
        return !session.live && !session.has_saved_state;
    }),
        sessions.end());

    if (error)
        error->clear();
    return sessions;
}

std::string format_session_listing_table(const std::vector<SessionSummary>& sessions)
{
    size_t id_width = std::string_view("SESSION ID").size();
    size_t state_width = std::string_view("STATE").size();
    size_t workspace_width = std::string_view("WORKSPACES").size();
    size_t pane_width = std::string_view("PANES").size();

    for (const auto& session : sessions)
    {
        id_width = std::max(id_width, session.session_id.size());
        state_width = std::max(state_width, std::char_traits<char>::length(session_state_text(session)));
    }

    std::ostringstream out;
    out << std::left << std::setw(static_cast<int>(id_width)) << "SESSION ID"
        << "  "
        << std::left << std::setw(static_cast<int>(state_width)) << "STATE"
        << "  "
        << std::left << std::setw(static_cast<int>(workspace_width)) << "WORKSPACES"
        << "  "
        << std::left << std::setw(static_cast<int>(pane_width)) << "PANES"
        << "  NAME\n";

    out << std::string(id_width, '-')
        << "  "
        << std::string(state_width, '-')
        << "  "
        << std::string(workspace_width, '-')
        << "  "
        << std::string(pane_width, '-')
        << "  ----\n";

    for (const auto& session : sessions)
    {
        out << std::left << std::setw(static_cast<int>(id_width)) << session.session_id
            << "  "
            << std::left << std::setw(static_cast<int>(state_width)) << session_state_text(session)
            << "  "
            << std::right << std::setw(static_cast<int>(workspace_width)) << session.workspace_count
            << "  "
            << std::right << std::setw(static_cast<int>(pane_width)) << session.pane_count
            << "  "
            << display_name_text(session)
            << '\n';
    }

    return out.str();
}

} // namespace draxul
