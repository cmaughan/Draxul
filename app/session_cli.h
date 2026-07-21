#pragma once

#include "cli_args.h"
#include "session_state.h"

#include <chrono>
#include <draxul/session_attach.h>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

enum class SessionCliMode
{
    Continue,
    List,
    Attach,
    Detach,
    Rename,
    Kill,
    Invalid,
};

struct SessionCliRequest
{
    SessionCliMode mode = SessionCliMode::Continue;
    std::string session_id;
    std::string session_name;

    static SessionCliRequest from_parsed_args(const ParsedArgs& args);
};

enum class SessionCliDisposition
{
    Continue,
    Handled,
    Error,
};

struct SessionCliResult
{
    SessionCliDisposition disposition = SessionCliDisposition::Continue;
    std::string output;
    std::string error;
};

struct SessionAttachAttempt
{
    bool attached = false;
    int status_code = 0;
};

struct SessionCliServices
{
    using AttachStatus = SessionAttachServer::AttachStatus;
    using Command = SessionAttachServer::Command;
    using LiveSessionInfo = SessionAttachServer::LiveSessionInfo;

    std::function<std::vector<SessionSummary>(std::string*)> list_sessions;
    std::function<AttachStatus(std::string_view, std::string*)> try_attach;
    std::function<AttachStatus(std::string_view, Command, std::string*)> send_command;
    std::function<bool(std::string_view, LiveSessionInfo*, std::string*)> query_live_session;
    std::function<bool(std::string_view, std::string_view, std::string*)> rename_live_session;
    std::function<bool(std::string_view, std::string_view, std::string*)> rename_saved_session;
    std::function<bool(std::string_view, std::string*)> delete_saved_state;
    std::function<bool(std::string_view, std::string*)> delete_runtime_metadata;
    std::function<std::chrono::steady_clock::time_point()> now;
    std::function<void(std::chrono::milliseconds)> sleep_for;
};

SessionCliServices make_default_session_cli_services();

class SessionCli
{
public:
    explicit SessionCli(SessionCliServices services = make_default_session_cli_services());

    SessionCliResult run(const SessionCliRequest& request) const;
    bool prepare_new_session_launch(ParsedArgs& args, std::string* error) const;
    SessionAttachAttempt try_attach_existing(
        std::string_view session_id, std::string* error) const;
    bool try_attach_with_retry(
        std::string_view session_id, std::chrono::milliseconds timeout, std::string* error) const;

private:
    SessionCliServices services_;
};

} // namespace draxul
