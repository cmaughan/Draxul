#include <draxul/cli_dispatch.h>
#include <draxul/cli_help.h>

namespace draxul
{

CliInvocation parse_command_line(const std::vector<std::string>& args)
{
    CliInvocation invocation;
    const auto integration = parse_integration_cli(args);
    if (integration.recognized)
    {
        invocation.needs_console = true;
        invocation.error = integration.error;
        if (integration.command)
            invocation.command = *integration.command;
        return invocation;
    }
    const auto topology = parse_topology_cli(args);
    if (topology.recognized)
    {
        invocation.needs_console = true;
        invocation.error_exit_code = 2;
        invocation.error = topology.error;
        if (topology.command)
            invocation.command = *topology.command;
        return invocation;
    }
    const auto control = parse_control_cli(args);
    if (control.recognized)
    {
        invocation.needs_console = true;
        invocation.error = control.error;
        if (control.command)
            invocation.command = *control.command;
        return invocation;
    }
    auto launch = parse_args(args);
    const auto& parsed = launch.args;
    invocation.needs_console = launch.error.has_value()
        || parsed.help || parsed.want_console || parsed.list_sessions
        || parsed.rename_session || parsed.delete_session
        || parsed.delete_all_sessions || parsed.server_status
        || parsed.shutdown_server || parsed.force_stop_server;
    invocation.error = std::move(launch.error);
    invocation.command = std::move(launch.args);
    return invocation;
}

std::optional<int> dispatch_cli(const CliInvocation& invocation, const CliContext& io)
{
    if (invocation.error)
    {
        std::fprintf(io.error, "%s\n", invocation.error->c_str());
        return invocation.error_exit_code;
    }
    if (const auto* command = std::get_if<IntegrationCliCommand>(&invocation.command))
        return run_integration_cli(*command, io);
    if (const auto* command = std::get_if<TopologyCliCommand>(&invocation.command))
        return run_topology_cli(*command, io);
    if (const auto* command = std::get_if<ControlCliCommand>(&invocation.command))
        return run_control_cli(*command, io);
    if (std::get<ParsedArgs>(invocation.command).help)
    {
        std::fputs(cli_help_text(), io.output);
        return 0;
    }
    return std::nullopt;
}

} // namespace draxul
