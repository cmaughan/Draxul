#include "session_cli.h"

#include "session_id.h"
#include "session_listing.h"

#include <draxul/log.h>

#include <thread>
#include <utility>

namespace draxul
{

namespace
{

bool rename_saved_session_records(
    std::string_view session_id, std::string_view session_name, std::string* error)
{
    bool updated = false;
    std::string io_error;
    if (auto state = load_session_state(session_id, &io_error))
    {
        state->session_name = std::string(session_name);
        if (!save_session_state(*state, &io_error))
        {
            if (error)
                *error = io_error;
            return false;
        }
        updated = true;
    }
    else if (!io_error.empty())
    {
        if (error)
            *error = io_error;
        return false;
    }

    io_error.clear();
    if (auto metadata = load_session_runtime_metadata(session_id, &io_error))
    {
        metadata->session_name = std::string(session_name);
        if (!save_session_runtime_metadata(*metadata, &io_error))
        {
            if (error)
                *error = io_error;
            return false;
        }
        updated = true;
    }
    else if (!io_error.empty())
    {
        if (error)
            *error = io_error;
        return false;
    }

    if (!updated && error)
        *error = "No running or saved session was found.";
    return updated;
}

SessionCliResult handled(std::string output)
{
    return { SessionCliDisposition::Handled, std::move(output), {} };
}

SessionCliResult failed(std::string error)
{
    return { SessionCliDisposition::Error, {}, std::move(error) };
}

} // namespace

SessionCliRequest SessionCliRequest::from_parsed_args(const ParsedArgs& args)
{
    SessionCliRequest request;
    request.session_id = args.session_id;
    request.session_name = args.session_name;

    const SessionCliMode modes[] = {
        args.list_sessions ? SessionCliMode::List : SessionCliMode::Continue,
        args.attach_session ? SessionCliMode::Attach : SessionCliMode::Continue,
        args.detach_session ? SessionCliMode::Detach : SessionCliMode::Continue,
        args.rename_session ? SessionCliMode::Rename : SessionCliMode::Continue,
        args.kill_session ? SessionCliMode::Kill : SessionCliMode::Continue,
    };
    int selected = 0;
    for (const auto mode : modes)
    {
        if (mode != SessionCliMode::Continue)
        {
            request.mode = mode;
            ++selected;
        }
    }
    if (selected > 1)
        request.mode = SessionCliMode::Invalid;
    return request;
}

SessionCliServices make_default_session_cli_services()
{
    return {
        .list_sessions = [](std::string* error) { return list_known_sessions(error); },
        .try_attach = [](std::string_view id, std::string* error) {
            return SessionAttachServer::try_attach(id, error);
        },
        .send_command = [](std::string_view id, SessionAttachServer::Command command, std::string* error) {
            return SessionAttachServer::send_command(id, command, error);
        },
        .query_live_session = [](std::string_view id, SessionAttachServer::LiveSessionInfo* info, std::string* error) {
            return SessionAttachServer::query_live_session(id, info, error);
        },
        .rename_live_session = [](std::string_view id, std::string_view name, std::string* error) {
            return SessionAttachServer::rename_session(id, name, error);
        },
        .rename_saved_session = rename_saved_session_records,
        .delete_saved_state = [](std::string_view id, std::string* error) {
            return delete_session_state(id, error);
        },
        .delete_runtime_metadata = [](std::string_view id, std::string* error) {
            return delete_session_runtime_metadata(id, error);
        },
        .now = [] { return std::chrono::steady_clock::now(); },
        .sleep_for = [](std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); },
    };
}

SessionCli::SessionCli(SessionCliServices services)
    : services_(std::move(services))
{
}

SessionCliResult SessionCli::run(const SessionCliRequest& request) const
{
    using AttachStatus = SessionAttachServer::AttachStatus;
    using Command = SessionAttachServer::Command;

    switch (request.mode)
    {
    case SessionCliMode::Continue:
        return {};
    case SessionCliMode::Invalid:
        return failed("error: multiple session CLI modes were requested\n");
    case SessionCliMode::List:
    {
        std::string list_error;
        const auto sessions = services_.list_sessions(&list_error);
        if (!list_error.empty())
            return failed("Failed to list sessions: " + list_error + "\n");
        if (sessions.empty())
            return handled("No saved sessions.\n");
        return handled(format_session_listing_table(sessions));
    }
    case SessionCliMode::Attach:
    {
        std::string attach_error;
        const auto status = services_.try_attach(request.session_id, &attach_error);
        if (status == AttachStatus::Attached)
            return handled("Attached to session '" + request.session_id + "'.\n");
        if (status == AttachStatus::NoServer)
            return failed("No running session '" + request.session_id + "'.\n");
        return failed("Failed to attach to session '" + request.session_id + "': "
            + (attach_error.empty() ? "unknown error" : attach_error) + "\n");
    }
    case SessionCliMode::Detach:
    {
        std::string command_error;
        const auto status = services_.send_command(request.session_id, Command::Detach, &command_error);
        if (status == AttachStatus::Attached)
        {
            constexpr auto kDetachTimeout = std::chrono::seconds(1);
            constexpr auto kDetachPoll = std::chrono::milliseconds(50);
            const auto deadline = services_.now() + kDetachTimeout;
            while (services_.now() < deadline)
            {
                SessionAttachServer::LiveSessionInfo live_info;
                if (services_.query_live_session(request.session_id, &live_info, nullptr)
                    && live_info.detached)
                    return handled("Detached session '" + request.session_id + "'.\n");
                services_.sleep_for(kDetachPoll);
            }
            return failed("Session '" + request.session_id
                + "' is running but did not detach. It may not be a detachable shell session.\n");
        }
        if (status == AttachStatus::NoServer)
            return failed("No running session '" + request.session_id + "'.\n");
        return failed("Failed to detach session '" + request.session_id + "': "
            + (command_error.empty() ? "unknown error" : command_error) + "\n");
    }
    case SessionCliMode::Rename:
    {
        std::string command_error;
        if (services_.rename_live_session(request.session_id, request.session_name, &command_error))
        {
            return handled("Renamed session '" + request.session_id + "' to '"
                + request.session_name + "'.\n");
        }

        std::string rename_error;
        if (services_.rename_saved_session(request.session_id, request.session_name, &rename_error))
        {
            return handled("Renamed saved session '" + request.session_id + "' to '"
                + request.session_name + "'.\n");
        }

        const std::string message = !rename_error.empty() ? rename_error
            : (!command_error.empty() ? command_error : "unknown error");
        return failed("Failed to rename session '" + request.session_id + "': " + message + "\n");
    }
    case SessionCliMode::Kill:
    {
        std::string command_error;
        const auto status = services_.send_command(request.session_id, Command::Shutdown, &command_error);
        if (status == AttachStatus::Attached)
        {
            (void)services_.delete_saved_state(request.session_id, nullptr);
            (void)services_.delete_runtime_metadata(request.session_id, nullptr);
            return handled("Killed running session '" + request.session_id + "'.\n");
        }

        std::string delete_error;
        const bool deleted_saved_state = services_.delete_saved_state(request.session_id, &delete_error);
        const bool deleted_metadata = services_.delete_runtime_metadata(request.session_id, nullptr);
        if (status == AttachStatus::NoServer && (deleted_saved_state || deleted_metadata))
            return handled("Deleted saved session '" + request.session_id + "'.\n");
        if (status == AttachStatus::NoServer)
            return failed("No running or saved session '" + request.session_id + "'.\n");
        return failed("Failed to kill session '" + request.session_id + "': "
            + (command_error.empty() ? "unknown error" : command_error) + "\n");
    }
    }
    return failed("error: invalid session CLI mode\n");
}

bool SessionCli::prepare_new_session_launch(ParsedArgs& args, std::string* error) const
{
    if (!args.new_session)
        return true;

    if (args.session_id_explicit)
    {
        const auto exists = session_id_exists(args.session_id);
        if (!exists)
        {
            if (error)
                *error = exists.error().message;
            return false;
        }
        if (*exists)
        {
            if (error)
            {
                *error = "Session '" + args.session_id
                    + "' already exists. Use --session with a different id or omit it to generate one.";
            }
            return false;
        }
        return true;
    }

    const int64_t unix_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
                                     .count();
    auto session_id = make_unique_session_id(args.session_name, unix_seconds);
    if (!session_id)
    {
        if (error)
            *error = session_id.error().message;
        return false;
    }
    args.session_id = *session_id;
    return true;
}

SessionAttachAttempt SessionCli::try_attach_existing(
    std::string_view session_id, std::string* error) const
{
    const auto status = services_.try_attach(session_id, error);
    return {
        .attached = status == SessionAttachServer::AttachStatus::Attached,
        .status_code = static_cast<int>(status),
    };
}

bool SessionCli::try_attach_with_retry(
    std::string_view session_id, std::chrono::milliseconds timeout, std::string* error) const
{
    constexpr auto kPoll = std::chrono::milliseconds(50);
    const auto deadline = services_.now() + timeout;
    std::string last_error;
    int attempt = 0;

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Retrying session attach for '%s' for up to %lld ms",
        std::string(session_id).c_str(),
        static_cast<long long>(timeout.count()));

    while (services_.now() < deadline)
    {
        ++attempt;
        last_error.clear();
        const auto status = services_.try_attach(session_id, &last_error);
        if (status == SessionAttachServer::AttachStatus::Attached)
        {
            DRAXUL_LOG_DEBUG(LogCategory::App,
                "Retry attach for '%s' succeeded on attempt %d",
                std::string(session_id).c_str(), attempt);
            return true;
        }
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "Retry attach attempt %d for '%s' returned status=%d error='%s'",
            attempt,
            std::string(session_id).c_str(),
            static_cast<int>(status),
            last_error.c_str());
        services_.sleep_for(kPoll);
    }

    if (error)
        *error = last_error;
    DRAXUL_LOG_WARN(LogCategory::App,
        "Retry attach for '%s' timed out after %d attempts; last error='%s'",
        std::string(session_id).c_str(), attempt, last_error.c_str());
    return false;
}

} // namespace draxul
