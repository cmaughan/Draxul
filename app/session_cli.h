#pragma once

#include "cli_args.h"

#include <functional>
#include <string>
#include <string_view>

namespace draxul
{

enum class SessionCliMode
{
    Continue,
    Rename,
    Delete,
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

struct SessionCliServices
{
    std::function<bool(std::string_view, std::string_view, std::string*)> rename_saved_session;
    std::function<bool(std::string_view, std::string*)> delete_saved_state;
};

SessionCliServices make_default_session_cli_services();

class SessionCli
{
public:
    explicit SessionCli(SessionCliServices services = make_default_session_cli_services());

    SessionCliResult run(const SessionCliRequest& request) const;
    bool prepare_new_session_launch(ParsedArgs& args, std::string* error) const;

private:
    SessionCliServices services_;
};

} // namespace draxul
