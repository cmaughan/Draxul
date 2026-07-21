#include "session_attach_internal.h"

#include <charconv>
#include <sstream>

namespace draxul::session_attach_detail
{

std::string_view command_text(SessionAttachServer::Command command)
{
    switch (command)
    {
    case SessionAttachServer::Command::Activate:
        return "activate";
    case SessionAttachServer::Command::Detach:
        return "detach";
    case SessionAttachServer::Command::Shutdown:
        return "shutdown";
    case SessionAttachServer::Command::QueryLiveSession:
        return "query-live-session";
    }
    return "activate";
}

std::string rename_request(std::string_view session_name)
{
    return std::string(kRenameSessionPrefix) + std::string(session_name);
}

ParsedRequest parse_request(std::string_view request)
{
    if (request == kStopInternalCommand)
        return { RequestKind::Stop };
    if (request == command_text(SessionAttachServer::Command::Activate))
        return { RequestKind::Command, SessionAttachServer::Command::Activate };
    if (request == command_text(SessionAttachServer::Command::Detach))
        return { RequestKind::Command, SessionAttachServer::Command::Detach };
    if (request == command_text(SessionAttachServer::Command::Shutdown))
        return { RequestKind::Command, SessionAttachServer::Command::Shutdown };
    if (request == command_text(SessionAttachServer::Command::QueryLiveSession))
        return { RequestKind::QueryLiveSession, SessionAttachServer::Command::QueryLiveSession };
    if (request.starts_with(kRenameSessionPrefix))
        return { RequestKind::Rename, SessionAttachServer::Command::Activate,
            request.substr(kRenameSessionPrefix.size()) };
    return {};
}

std::string serialize_live_session_info(const SessionAttachServer::LiveSessionInfo& info)
{
    std::ostringstream out;
    out << "workspace_count=" << info.workspace_count << '\n';
    out << "pane_count=" << info.pane_count << '\n';
    out << "detached=" << (info.detached ? 1 : 0) << '\n';
    out << "owner_pid=" << info.owner_pid << '\n';
    out << "last_attached_unix_s=" << info.last_attached_unix_s << '\n';
    out << "last_detached_unix_s=" << info.last_detached_unix_s << '\n';
    return out.str();
}

template <typename T>
bool parse_integral(std::string_view text, T* value)
{
    T parsed{};
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end)
        return false;
    *value = parsed;
    return true;
}

bool parse_live_session_info(std::string_view payload,
    SessionAttachServer::LiveSessionInfo* info, std::string* error)
{
    enum Field : unsigned
    {
        WorkspaceCount = 1u << 0,
        PaneCount = 1u << 1,
        Detached = 1u << 2,
        OwnerPid = 1u << 3,
        LastAttached = 1u << 4,
        LastDetached = 1u << 5,
    };
    constexpr unsigned kAllFields
        = WorkspaceCount | PaneCount | Detached | OwnerPid | LastAttached | LastDetached;

    SessionAttachServer::LiveSessionInfo parsed;
    unsigned fields = 0;
    bool malformed = false;
    while (!payload.empty())
    {
        const size_t newline = payload.find('\n');
        const std::string_view line = newline == std::string_view::npos
            ? payload
            : payload.substr(0, newline);
        payload = newline == std::string_view::npos ? std::string_view{} : payload.substr(newline + 1);
        if (line.empty())
            continue;

        const size_t equals = line.find('=');
        if (equals == std::string_view::npos)
        {
            if (error)
                *error = "Malformed live-session response line.";
            return false;
        }

        const std::string_view key = line.substr(0, equals);
        const std::string_view value = line.substr(equals + 1);
        if (key == "workspace_count")
        {
            if (!parse_integral(value, &parsed.workspace_count))
            {
                malformed = true;
                break;
            }
            fields |= WorkspaceCount;
        }
        else if (key == "pane_count")
        {
            if (!parse_integral(value, &parsed.pane_count))
            {
                malformed = true;
                break;
            }
            fields |= PaneCount;
        }
        else if (key == "detached")
        {
            if (value == "1" || value == "true")
                parsed.detached = true;
            else if (value == "0" || value == "false")
                parsed.detached = false;
            else
            {
                malformed = true;
                break;
            }
            fields |= Detached;
        }
        else if (key == "owner_pid")
        {
            if (!parse_integral(value, &parsed.owner_pid))
            {
                malformed = true;
                break;
            }
            fields |= OwnerPid;
        }
        else if (key == "last_attached_unix_s")
        {
            if (!parse_integral(value, &parsed.last_attached_unix_s))
            {
                malformed = true;
                break;
            }
            fields |= LastAttached;
        }
        else if (key == "last_detached_unix_s")
        {
            if (!parse_integral(value, &parsed.last_detached_unix_s))
            {
                malformed = true;
                break;
            }
            fields |= LastDetached;
        }
    }

    if (malformed || fields != kAllFields)
    {
        if (error)
            *error = "Malformed or incomplete live-session response.";
        return false;
    }

    if (info)
        *info = parsed;
    return true;
}

} // namespace draxul::session_attach_detail
