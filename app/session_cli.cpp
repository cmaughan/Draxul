#include "session_cli.h"

#include "session_id.h"
#include "session_state.h"

#include <chrono>
#include <utility>

namespace draxul
{

namespace
{

bool rename_saved_session_record(
    std::string_view session_id, std::string_view session_name, std::string* error)
{
    std::string io_error;
    auto state = load_session_state(session_id, &io_error);
    if (!state)
    {
        if (error)
            *error = io_error.empty() ? "No saved session was found." : io_error;
        return false;
    }

    state->session_name = std::string(session_name);
    if (!save_session_state(*state, &io_error))
    {
        if (error)
            *error = io_error;
        return false;
    }
    if (error)
        error->clear();
    return true;
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
        args.rename_session ? SessionCliMode::Rename : SessionCliMode::Continue,
        args.delete_session ? SessionCliMode::Delete : SessionCliMode::Continue,
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
        .rename_saved_session = rename_saved_session_record,
        .delete_saved_state = [](std::string_view id, std::string* error) {
            return delete_session_state(id, error);
        },
    };
}

SessionCli::SessionCli(SessionCliServices services)
    : services_(std::move(services))
{
}

SessionCliResult SessionCli::run(const SessionCliRequest& request) const
{
    switch (request.mode)
    {
    case SessionCliMode::Continue:
        return {};
    case SessionCliMode::Invalid:
        return failed("error: multiple session CLI modes were requested\n");
    case SessionCliMode::Rename:
    {
        std::string rename_error;
        if (services_.rename_saved_session(request.session_id, request.session_name, &rename_error))
        {
            return handled("Renamed saved session '" + request.session_id + "' to '"
                + request.session_name + "'.\n");
        }
        return failed("Failed to rename saved session '" + request.session_id + "': "
            + (rename_error.empty() ? "unknown error" : rename_error) + "\n");
    }
    case SessionCliMode::Delete:
    {
        std::string delete_error;
        if (services_.delete_saved_state(request.session_id, &delete_error))
            return handled("Deleted saved session '" + request.session_id + "'.\n");
        return failed("Failed to delete saved session '" + request.session_id + "': "
            + (delete_error.empty() ? "session not found" : delete_error) + "\n");
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

} // namespace draxul
