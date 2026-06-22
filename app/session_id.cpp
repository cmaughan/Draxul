#include "session_id.h"

#include "session_state.h"

#include <cctype>
#include <cstdio>
#include <ctime>
#include <draxul/session_attach.h>

namespace draxul
{

std::string make_session_id_slug(std::string_view text)
{
    std::string slug;
    slug.reserve(text.size());
    bool last_was_separator = false;
    for (unsigned char ch : text)
    {
        if (std::isalnum(ch))
        {
            slug.push_back(static_cast<char>(std::tolower(ch)));
            last_was_separator = false;
        }
        else if (!last_was_separator && !slug.empty())
        {
            slug.push_back('-');
            last_was_separator = true;
        }
    }

    while (!slug.empty() && slug.back() == '-')
        slug.pop_back();
    if (slug.empty())
        slug = "session";
    if (slug.size() > 40)
    {
        slug.resize(40);
        while (!slug.empty() && slug.back() == '-')
            slug.pop_back();
        if (slug.empty())
            slug = "session";
    }
    return slug;
}

std::string format_session_id_timestamp(int64_t unix_seconds)
{
    const std::time_t raw = static_cast<std::time_t>(unix_seconds);
    std::tm local = {};
#ifdef _WIN32
    localtime_s(&local, &raw);
#else
    localtime_r(&raw, &local);
#endif

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d%02d%02d-%02d%02d%02d",
        local.tm_year + 1900,
        local.tm_mon + 1,
        local.tm_mday,
        local.tm_hour,
        local.tm_min,
        local.tm_sec);
    return buffer;
}

std::string make_session_id_base(std::string_view display_name, int64_t unix_seconds)
{
    return make_session_id_slug(display_name) + "-" + format_session_id_timestamp(unix_seconds);
}

std::string make_session_id_candidate(std::string_view base, int suffix)
{
    if (suffix <= 1)
        return std::string(base);
    return std::string(base) + "-" + std::to_string(suffix);
}

Result<bool, Error> session_id_exists(std::string_view session_id)
{
    std::string probe_error;
    const auto probe_status = SessionAttachServer::probe(session_id, &probe_error);
    if (probe_status == SessionAttachServer::ProbeStatus::Running)
        return true;
    if (probe_status == SessionAttachServer::ProbeStatus::Error)
    {
        return Result<bool, Error>::err(Error::io(
            probe_error.empty() ? "Failed probing for an existing session." : probe_error));
    }

    std::string io_error;
    if (has_saved_session_state(session_id, &io_error))
        return true;
    if (!io_error.empty())
        return Result<bool, Error>::err(Error::io(io_error));

    (void)clear_session_runtime_liveness(session_id);
    return false;
}

Result<std::string, Error> make_unique_session_id(
    std::string_view display_name,
    int64_t unix_seconds)
{
    const std::string base = make_session_id_base(display_name, unix_seconds);
    for (int suffix = 1;; ++suffix)
    {
        const std::string candidate = make_session_id_candidate(base, suffix);
        auto exists = session_id_exists(candidate);
        if (!exists)
            return Result<std::string, Error>::err(exists.error());
        if (!*exists)
            return candidate;
    }
}

} // namespace draxul
