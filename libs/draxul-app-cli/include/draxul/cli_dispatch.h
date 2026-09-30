#pragma once

#include <draxul/cli_args.h>
#include <draxul/cli_context.h>
#include <draxul/control_cli.h>
#include <draxul/integration_cli.h>
#include <draxul/topology_cli.h>
#include <variant>

namespace draxul
{

struct CliInvocation
{
    std::variant<ParsedArgs, IntegrationCliCommand, TopologyCliCommand, ControlCliCommand> command;
    std::optional<std::string> error;
    int error_exit_code = 1;
    bool needs_console = false;
};

// Parsing precedes native console setup; execution starts only after the caller
// has attached its console. This preserves redirected Windows stdout/stderr.
CliInvocation parse_command_line(const std::vector<std::string>& args);
// A disengaged result means the native entry point should launch the app/server.
std::optional<int> dispatch_cli(const CliInvocation& invocation, const CliContext& io = {});

} // namespace draxul
