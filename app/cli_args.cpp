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
        if (args[i] == "--console")
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
        else if (args[i] == "--experimental-server-client")
            parsed.experimental_server_client = true;
        else if (args[i] == "--experimental-remote-terminal")
        {
            parsed.experimental_remote_terminal = true;
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
    const int session_mode_count = (parsed.list_sessions ? 1 : 0)
        + (parsed.new_session ? 1 : 0)
        + (parsed.rename_session ? 1 : 0)
        + (parsed.delete_session ? 1 : 0);
    if (session_mode_count > 1)
    {
        result.error
            = "error: choose only one of --list-sessions, --new-session, --rename-session, or --delete-session";
        return result;
    }
    const int server_mode_count = (parsed.server ? 1 : 0)
        + (parsed.server_status ? 1 : 0)
        + (parsed.shutdown_server ? 1 : 0);
    if (server_mode_count > 1)
    {
        result.error
            = "error: choose only one of --server, --server-status, or --shutdown-server";
        return result;
    }
    if (server_mode_count > 0 && parsed.host_kind)
    {
        result.error = "error: server control modes cannot be combined with --host";
        return result;
    }
    if (parsed.experimental_remote_terminal
        && (parsed.no_server || server_mode_count > 0
            || parsed.host_kind.has_value() || parsed.smoke_test
            || session_mode_count > 0 || !parsed.screenshot_path.empty()))
    {
        result.error
            = "error: --experimental-remote-terminal cannot be combined with standalone, server-control, Session-control, smoke, or screenshot modes";
        return result;
    }
#ifdef DRAXUL_ENABLE_RENDER_TESTS
    if (parsed.experimental_remote_terminal
        && (!parsed.render_test_path.empty()
            || !parsed.export_render_test_path.empty()))
    {
        result.error
            = "error: --experimental-remote-terminal cannot be combined with render-test modes";
        return result;
    }
#endif
    return result;
}

bool should_bootstrap_experimental_server(const ParsedArgs& args)
{
    if (!args.experimental_server_client || args.no_server
        || args.server || args.server_status || args.shutdown_server
        || args.host_kind.has_value() || args.smoke_test
        || args.list_sessions || args.new_session || args.rename_session
        || args.delete_session || !args.screenshot_path.empty())
    {
        return false;
    }
#ifdef DRAXUL_ENABLE_RENDER_TESTS
    if (!args.render_test_path.empty() || !args.export_render_test_path.empty())
        return false;
#endif
    return true;
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
