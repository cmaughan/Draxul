#include "cli_args.h"

#include <draxul/host_registry.h>
#include <exception>
#include <string>

namespace draxul
{

ParseArgsResult parse_args(const std::vector<std::string>& args)
{
    ParseArgsResult result;
    ParsedArgs& parsed = result.args;

    for (size_t i = 1; i < args.size(); ++i)
    {
        if (args[i] == "--help" || args[i] == "-h")
            parsed.help = true;
        else if (args[i] == "--console")
            parsed.want_console = true;
        else if (args[i] == "--smoke-test")
            parsed.smoke_test = true;
        else if (args[i] == "--continuous-refresh")
            parsed.continuous_refresh = true;
        else if (args[i] == "--no-vblank")
            parsed.no_vblank = true;
        else if (args[i] == "--no-ui")
            parsed.no_ui = true;
        else if (args[i] == "--list-sessions")
            parsed.list_sessions = true;
        else if (args[i] == "--new-session")
            parsed.new_session = true;
        else if (args[i] == "--rename-session")
            parsed.rename_session = true;
        else if (args[i] == "--delete-session")
            parsed.delete_session = true;
        else if (args[i] == "--server")
            parsed.server = true;
        else if (args[i] == "--server-status")
            parsed.server_status = true;
        else if (args[i] == "--shutdown-server")
            parsed.shutdown_server = true;
        else if (args[i] == "--force-stop-server")
            parsed.force_stop_server = true;
        else if (args[i] == "--yes")
            parsed.confirmed = true;
        else if (args[i] == "--experimental-server-client")
            parsed.experimental_server_client = true;
        else if (args[i] == "--experimental-remote-terminal")
        {
            parsed.experimental_remote_terminal = true;
            parsed.experimental_server_client = true;
        }
        else if (args[i] == "--experimental-remote-shell")
        {
            parsed.experimental_remote_shell = true;
            parsed.experimental_server_client = true;
        }
        else if (args[i] == "--no-server")
            parsed.no_server = true;
        else if (args[i] == "--json")
            parsed.json_output = true;
        else if (args[i] == "--server-runtime-dir" && i + 1 < args.size())
        {
            ++i;
            parsed.server_runtime_dir = args[i];
            if (parsed.server_runtime_dir.empty())
            {
                result.error = "error: --server-runtime-dir requires a non-empty path";
                return result;
            }
        }
        else if (args[i] == "--server-runtime-dir")
        {
            result.error = "error: --server-runtime-dir requires a path";
            return result;
        }
        else if (args[i] == "--server-shell" && i + 1 < args.size())
        {
            parsed.server_shell_kind = args[++i];
            if (parsed.server_shell_kind != "powershell"
                && parsed.server_shell_kind != "bash"
                && parsed.server_shell_kind != "zsh"
                && parsed.server_shell_kind != "wsl")
            {
                result.error
                    = "error: --server-shell must be powershell, bash, zsh, or wsl";
                return result;
            }
        }
        else if (args[i] == "--server-shell")
        {
            result.error = "error: --server-shell requires a shell kind";
            return result;
        }
        else if (args[i] == "--server-command" && i + 1 < args.size())
        {
            parsed.server_command = args[++i];
            if (parsed.server_command.empty())
            {
                result.error
                    = "error: --server-command requires a non-empty command";
                return result;
            }
        }
        else if (args[i] == "--server-command")
        {
            result.error
                = "error: --server-command requires a command";
            return result;
        }
        else if (args[i] == "--server-working-dir" && i + 1 < args.size())
        {
            parsed.server_working_dir = args[++i];
            if (parsed.server_working_dir.empty())
            {
                result.error
                    = "error: --server-working-dir requires a non-empty path";
                return result;
            }
        }
        else if (args[i] == "--server-working-dir")
        {
            result.error = "error: --server-working-dir requires a path";
            return result;
        }
        else if (args[i] == "--server-scrollback-lines"
            && i + 1 < args.size())
        {
            try
            {
                parsed.server_scrollback_lines = std::stoi(args[++i]);
            }
            catch (const std::exception&)
            {
                result.error
                    = "error: --server-scrollback-lines requires an integer";
                return result;
            }
            if (parsed.server_scrollback_lines < 0
                || parsed.server_scrollback_lines > 1000000)
            {
                result.error
                    = "error: --server-scrollback-lines must be between 0 and 1000000";
                return result;
            }
        }
        else if (args[i] == "--server-scrollback-lines")
        {
            result.error
                = "error: --server-scrollback-lines requires an integer";
            return result;
        }
#ifdef DRAXUL_ENABLE_RENDER_TESTS
        else if (args[i] == "--bless-render-test")
            parsed.bless_render_test = true;
        else if (args[i] == "--show-render-test-window")
            parsed.show_render_test_window = true;
        else if (args[i] == "--render-test" && i + 1 < args.size())
        {
            ++i;
            parsed.render_test_path = args[i];
        }
        else if (args[i] == "--export-render-test" && i + 1 < args.size())
        {
            ++i;
            parsed.export_render_test_path = args[i];
        }
#endif
        else if (args[i] == "--host" && i + 1 < args.size())
        {
            ++i;
            parsed.host_kind = parse_host_kind(args[i]);
            if (!parsed.host_kind)
            {
                result.error = "error: unknown --host value '" + args[i] + "'";
                return result;
            }
        }
        else if (args[i] == "--command" && i + 1 < args.size())
        {
            ++i;
            parsed.host_command = args[i];
        }
        else if (args[i] == "--source" && i + 1 < args.size())
        {
            ++i;
            parsed.host_source_path = args[i];
        }
        else if (args[i] == "--session" && i + 1 < args.size())
        {
            ++i;
            parsed.session_id = args[i];
            parsed.session_id_explicit = true;
            if (parsed.session_id.empty())
            {
                result.error = "error: --session requires a non-empty session id";
                return result;
            }
        }
        else if (args[i] == "--session-name" && i + 1 < args.size())
        {
            ++i;
            parsed.session_name = args[i];
            if (parsed.session_name.empty())
            {
                result.error = "error: --session-name requires a non-empty session name";
                return result;
            }
        }
        else if (args[i] == "--log-file" && i + 1 < args.size())
        {
            ++i;
            parsed.log_file = args[i];
        }
        else if (args[i] == "--log-level" && i + 1 < args.size())
        {
            ++i;
            parsed.log_level = args[i];
        }
        else if (args[i] == "--pty-capture-file" && i + 1 < args.size())
        {
            ++i;
            parsed.pty_capture_file = args[i];
            if (parsed.pty_capture_file.empty())
            {
                result.error = "error: --pty-capture-file requires a non-empty path";
                return result;
            }
        }
        else if (args[i] == "--screenshot" && i + 1 < args.size())
        {
            ++i;
            parsed.screenshot_path = args[i];
        }
        else if (args[i] == "--gui-action" && i + 1 < args.size())
        {
            ++i;
            parsed.gui_action = args[i];
        }
        else if (args[i] == "--screenshot-delay" && i + 1 < args.size())
        {
            ++i;
            try
            {
                size_t consumed = 0;
                int value = std::stoi(args[i], &consumed);
                if (consumed != args[i].size() || value < 0)
                {
                    result.error = "error: --screenshot-delay requires a non-negative integer";
                    return result;
                }
                parsed.screenshot_delay_ms = value;
            }
            catch (const std::exception&)
            {
                result.error = "error: --screenshot-delay requires a non-negative integer";
                return result;
            }
        }
        else if (args[i] == "--screenshot-size" && i + 1 < args.size())
        {
            ++i;
            const auto& size_str = args[i];
            auto x_pos = size_str.find('x');
            if (x_pos == std::string::npos)
            {
                result.error = "error: --screenshot-size requires valid dimensions (e.g. 800x600)";
                return result;
            }
            try
            {
                const std::string width_str = size_str.substr(0, x_pos);
                const std::string height_str = size_str.substr(x_pos + 1);
                size_t w_consumed = 0;
                size_t h_consumed = 0;
                parsed.screenshot_width = std::stoi(width_str, &w_consumed);
                parsed.screenshot_height = std::stoi(height_str, &h_consumed);
                if (w_consumed != width_str.size() || h_consumed != height_str.size())
                {
                    result.error = "error: --screenshot-size requires valid dimensions (e.g. 800x600)";
                    return result;
                }
                if (parsed.screenshot_width <= 0 || parsed.screenshot_height <= 0)
                {
                    result.error = "error: --screenshot-size requires positive dimensions (e.g. 800x600)";
                    return result;
                }
            }
            catch (const std::exception&)
            {
                result.error = "error: --screenshot-size requires valid dimensions (e.g. 800x600)";
                return result;
            }
        }
    }
    if (parsed.rename_session && parsed.session_name.empty())
    {
        result.error = "error: --rename-session requires --session-name <name>";
        return result;
    }
    if (parsed.delete_session && !parsed.session_id_explicit)
    {
        result.error
            = "error: --delete-session requires --session <id>";
        return result;
    }
    if (parsed.new_session && parsed.rename_session)
    {
        result.error
            = "error: choose only one of --new-session or --rename-session";
        return result;
    }
    const int session_mode_count = parsed.new_session ? 1 : 0;
    const int server_mode_count = (parsed.server ? 1 : 0)
        + (parsed.server_status ? 1 : 0)
        + (parsed.list_sessions ? 1 : 0)
        + (parsed.rename_session ? 1 : 0)
        + (parsed.delete_session ? 1 : 0)
        + (parsed.shutdown_server ? 1 : 0)
        + (parsed.force_stop_server ? 1 : 0);
    if (server_mode_count > 1)
    {
        result.error
            = "error: choose only one of --server, --server-status, --list-sessions, --rename-session, --delete-session, --shutdown-server, or --force-stop-server";
        return result;
    }
    if (server_mode_count > 0 && session_mode_count > 0)
    {
        result.error
            = "error: server control modes cannot be combined with Session-control modes";
        return result;
    }
    if (server_mode_count > 0 && parsed.host_kind)
    {
        result.error = "error: server control modes cannot be combined with --host";
        return result;
    }
    const bool remote_terminal_mode
        = parsed.experimental_remote_terminal || parsed.experimental_remote_shell;
    if (parsed.experimental_remote_terminal && parsed.experimental_remote_shell)
    {
        result.error
            = "error: choose only one of --experimental-remote-terminal or --experimental-remote-shell";
        return result;
    }
    if (remote_terminal_mode
        && (parsed.no_server || server_mode_count > 0
            || parsed.host_kind.has_value() || parsed.smoke_test
            || session_mode_count > 0 || !parsed.screenshot_path.empty()))
    {
        result.error
            = "error: remote terminal modes cannot be combined with standalone, server-control, Session-control, smoke, or screenshot modes";
        return result;
    }
    if (parsed.confirmed
        && !parsed.shutdown_server
        && !parsed.delete_session
        && !parsed.force_stop_server)
    {
        result.error
            = "error: --yes is only valid with --delete-session, --shutdown-server, or --force-stop-server";
        return result;
    }
    if (parsed.force_stop_server && !parsed.confirmed)
    {
        result.error
            = "error: --force-stop-server requires --yes";
        return result;
    }
#ifdef DRAXUL_ENABLE_RENDER_TESTS
    if (remote_terminal_mode
        && (!parsed.render_test_path.empty()
            || !parsed.export_render_test_path.empty()))
    {
        result.error
            = "error: remote terminal modes cannot be combined with render-test modes";
        return result;
    }
#endif
    return result;
}

bool is_server_owned_shell_kind(HostKind kind)
{
    switch (kind)
    {
    case HostKind::PowerShell:
    case HostKind::Bash:
    case HostKind::Zsh:
    case HostKind::Wsl:
    case HostKind::RemoteTerminal:
        return true;
    case HostKind::Nvim:
    case HostKind::MegaCity:
    case HostKind::BioView:
    case HostKind::SatView:
    case HostKind::NanoVGDemo:
    case HostKind::Markdown:
    case HostKind::Kanban:
    case HostKind::Score:
        return false;
    }
    return false;
}

bool should_use_shared_server(const ParsedArgs& args)
{
    if (args.help || args.no_server
        || args.server || args.server_status
        || args.shutdown_server || args.force_stop_server
        || args.smoke_test || args.list_sessions
        || args.rename_session || args.delete_session
        || !args.screenshot_path.empty()
        || !args.host_source_path.empty())
    {
        return false;
    }
#ifdef DRAXUL_ENABLE_RENDER_TESTS
    if (!args.render_test_path.empty()
        || !args.export_render_test_path.empty())
    {
        return false;
    }
#endif
    if (args.experimental_remote_terminal
        || args.experimental_remote_shell)
    {
        return true;
    }
    return !args.host_kind
        || is_server_owned_shell_kind(*args.host_kind);
}

bool should_bootstrap_experimental_server(const ParsedArgs& args)
{
    return args.experimental_server_client
        && should_use_shared_server(args);
}

std::optional<std::string> validate_host_provider_availability(
    const ParsedArgs& args, const HostProviderRegistry& registry)
{
    if (!args.host_kind)
        return std::nullopt;
    if (const auto* metadata = registry.metadata(*args.host_kind);
        metadata != nullptr
        && supports_launch_context(metadata->launch_contexts, HostLaunchContext::Cli))
    {
        return std::nullopt;
    }
    return "error: host '" + std::string(to_string(*args.host_kind))
        + "' is not available in this build; available hosts: " + registry.available_cli_names();
}

} // namespace draxul
