#include <draxul/launch_policy.h>
#include <draxul/config_document.h>
#include <draxul/executable_layout.h>
#include <draxul/session_id.h>
#include <algorithm>

namespace draxul
{

std::filesystem::path cli_server_runtime_directory(const ParsedArgs& parsed)
{
    if (!parsed.server_runtime_dir.empty())
        return parsed.server_runtime_dir;
    return server_runtime_directory(
        ConfigDocument::default_path().parent_path());
}

bool is_server_control_launch(const ParsedArgs& parsed)
{
    return parsed.server || parsed.server_status || parsed.list_sessions
        || parsed.rename_session || parsed.delete_session || parsed.delete_all_sessions
        || parsed.shutdown_server || parsed.force_stop_server || parsed.server_stop_dialog;
}

std::string describe_server_startup_failure(const ServerProbeResult& server_result,
    const std::filesystem::path& connected_server_runtime)
{
    // Only claim a live server for states where one exists; for
    // Absent/LaunchFailed/Crashed/Stale that advice was actively
    // wrong ("stop the server" when none is running) and hid the
    // real evidence: the server log next to the runtime endpoint.
    const bool server_probably_alive
        = server_result.state == draxul::ServerProbeState::Busy
        || server_result.state == draxul::ServerProbeState::Starting
        || server_result.state
            == draxul::ServerProbeState::Incompatible;
    const std::string remediation
        = server_result.error_code == "runtime_unavailable"
        ? "\n\nCheck that your user account can access "
            + connected_server_runtime.string() + "."
        : server_probably_alive
        ? "\n\nThe existing server was left running. "
          "Stop it explicitly before retrying."
        : "\n\nNo running server was found. See "
            + draxul::default_server_log_path(
                connected_server_runtime)
                  .string()
            + " for the server's own record of what happened.";
    return std::string(
        "Could not connect to the Draxul server ("
        + std::string(
            draxul::to_string(server_result.state))
        + "): " + server_result.error_message + remediation);
}

std::optional<std::string> validate_server_launch_capabilities(
    const ParsedArgs& parsed, const ServerWelcome& welcome)
{
    const bool fake_remote_terminal = parsed.experimental_remote_terminal;
    const bool real_remote_terminal = should_use_shared_server(parsed) && !fake_remote_terminal;
    if (fake_remote_terminal
        && std::ranges::find(welcome.capabilities,
               "fake-remote-terminal")
            == welcome.capabilities.end())
    {
        return std::string(
            "The running Draxul server does not support "
            "the diagnostic fake terminal. Stop it and retry.");
    }
    if (real_remote_terminal
        && std::ranges::find(welcome.capabilities,
               "real-remote-terminal")
            == welcome.capabilities.end())
    {
        return std::string(
            "The running Draxul server does not support "
            "shared shells. Stop it and retry.");
    }
    if (real_remote_terminal
        && std::ranges::find(welcome.capabilities,
               "topology-v1")
            == welcome.capabilities.end())
    {
        return std::string(
            "The running Draxul server does not support "
            "shared topology. Stop it and retry.");
    }
    if (real_remote_terminal
        && std::ranges::find(welcome.capabilities,
               "multi-terminal-v1")
            == welcome.capabilities.end())
    {
        return std::string(
            "The running Draxul server does not support "
            "multiple shared terminals. Stop it and retry.");
    }
    if (real_remote_terminal
        && parsed.session_id != "default"
        && std::ranges::find(welcome.capabilities,
               "named-sessions-v1")
            == welcome.capabilities.end())
    {
        return std::string(
            "The running Draxul server does not support "
            "named Sessions. Stop it and retry.");
    }
    if (real_remote_terminal
        && std::ranges::find(
               welcome.capabilities,
               "agent-projection-v1")
            == welcome.capabilities.end())
    {
        return std::string(
            "The running Draxul server does not support "
            "shared Agents. Stop it and retry.");
    }
    if (real_remote_terminal
        && std::ranges::find(
               welcome.capabilities,
               "agent-control-v1")
            == welcome.capabilities.end())
    {
        return std::string(
            "The running Draxul server does not support "
            "headless Agent control. Stop it and retry.");
    }
    if (real_remote_terminal
        && std::ranges::find(
               welcome.capabilities,
               "managed-agent-v1")
            == welcome.capabilities.end())
    {
        return std::string(
            "The running Draxul server does not support "
            "managed Agents. Stop it and retry.");
    }
    if (!parsed.plugin_id.empty()
        && std::ranges::find(
               welcome.capabilities,
               "client-plugin-pane-v1")
            == welcome.capabilities.end())
    {
        return std::string(
            "The running Draxul server does not support plugin panes. "
            "Stop it and retry with this Draxul build.");
    }
    return std::nullopt;
}

std::optional<std::string> resolve_new_session_id(ParsedArgs& parsed,
    const ServerStatusSnapshot& status, int64_t unix_seconds)
{
    const auto session_exists
        = [&status](std::string_view id) {
              return std::ranges::any_of(
                  status.session_statuses,
                  [id](const auto& session) {
                      return session.session_id == id;
                  });
          };
    if (parsed.session_id_explicit)
    {
        if (session_exists(parsed.session_id))
        {
            return std::string(
                "Session '" + parsed.session_id
                + "' already exists. Choose another id.");
        }
    }
    else
    {
        const std::string base
            = draxul::make_session_id_base(
                parsed.session_name, unix_seconds);
        int suffix = 1;
        do
        {
            parsed.session_id
                = draxul::make_session_id_candidate(
                    base, suffix++);
        } while (session_exists(parsed.session_id));
    }

    return std::nullopt;
}

} // namespace draxul
