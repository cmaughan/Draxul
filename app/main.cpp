#include "agent_integration.h"
#include "app.h"
#include "cli_args.h"
#include "cli_help.h"
#include "control_cli.h"
#include "server_status_surface.h"
#include "session_id.h"
#include "topology_cli.h"
#ifdef __APPLE__
#include "macos_server_status_surface.h"
#include <unistd.h>
#endif
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <draxul/bmp.h>
#include <draxul/client_recovery.h>
#include <draxul/config_document.h>
#include <draxul/host_registry.h>
#include <draxul/kanban/kanban_host.h>
#include <draxul/log.h>
#include <draxul/markdown/markdown_host.h>
#include <draxul/nanovg_demo_host.h>
#include <draxul/perf_timing.h>
#include <draxul/plugin_host.h>
#include <draxul/plugin_manager.h>
#include <draxul/remote_terminal_host.h>
#include <draxul/runtime_path.h>
#include <draxul/server_client.h>
#include <draxul/server_kernel.h>
#include <draxul/server_protocol.h>
#include <draxul/topology_client.h>
#ifdef DRAXUL_ENABLE_RENDER_TESTS
#include <draxul/render_test.h>
#endif
#include <algorithm>
#include <atomic>
#include <csignal>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <cstdio>
#include <windows.h>

// This must come after shellapi!
#include <shellapi.h>
#endif

namespace
{

#ifdef _WIN32
void ensure_valid_standard_stream(FILE* stream, DWORD standard_handle,
    const char* mode)
{
    const HANDLE handle = GetStdHandle(standard_handle);
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
        return;

    // A WIN32_EXECUTABLE launched from Explorer has no standard handles.
    // UCRT treats writes to those unbound FILE streams as an invalid-parameter
    // failure and terminates the process, so give diagnostics a harmless sink
    // until --console attaches or allocates a real console below.
    FILE* replacement = nullptr;
    freopen_s(&replacement, "NUL", mode, stream);
}

void ensure_valid_standard_io()
{
    ensure_valid_standard_stream(
        stdout, STD_OUTPUT_HANDLE, "w");
    ensure_valid_standard_stream(
        stderr, STD_ERROR_HANDLE, "w");
}

bool standard_handle_is_redirected(DWORD standard_handle)
{
    const HANDLE handle = GetStdHandle(standard_handle);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
        return false;
    const DWORD type = GetFileType(handle);
    return type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK;
}

void ensure_console_io(bool allow_alloc_console)
{
    static bool configured = false;
    if (configured)
        return;

    // ProcessStartInfo, CI runners, and other agents commonly launch the
    // WIN32 executable with redirected pipes. Attaching to a real (possibly
    // hidden) parent console must not replace those routes with CONIN$/CONOUT$.
    const bool redirected_stdin
        = standard_handle_is_redirected(STD_INPUT_HANDLE);
    const bool redirected_stdout
        = standard_handle_is_redirected(STD_OUTPUT_HANDLE);
    const bool redirected_stderr
        = standard_handle_is_redirected(STD_ERROR_HANDLE);

    if (!GetConsoleWindow())
    {
        if (!AttachConsole(ATTACH_PARENT_PROCESS) && allow_alloc_console)
            AllocConsole();
    }

    if (GetConsoleWindow())
    {
        FILE* stream = nullptr;
        if (!redirected_stdout)
            freopen_s(&stream, "CONOUT$", "w", stdout);
        if (!redirected_stderr)
            freopen_s(&stream, "CONOUT$", "w", stderr);
        if (!redirected_stdin)
            freopen_s(&stream, "CONIN$", "r", stdin);
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        configured = true;
    }
}

std::vector<std::string> command_line_args()
{
    PERF_MEASURE();
    std::vector<std::string> args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return args;

    for (int i = 0; i < argc; ++i)
    {
        int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (size <= 1)
        {
            args.emplace_back();
            continue;
        }
        std::string converted(size, '\0');
        int written = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, converted.data(), size, nullptr, nullptr);
        if (written <= 0)
        {
            args.emplace_back();
            continue;
        }
        converted.pop_back(); // remove trailing NUL embedded by WideCharToMultiByte
        args.push_back(std::move(converted));
    }
    LocalFree(argv);
    return args;
}
#else
std::vector<std::string> command_line_args(int argc, char* argv[])
{
    PERF_MEASURE();
    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i)
        args.emplace_back(argv[i]);
    return args;
}
#endif

// CLI parsing has moved to app/cli_args.{h,cpp} so it can be unit-tested
// without spawning a subprocess. See draxul::parse_args() / ParseArgsResult.

std::filesystem::path executable_dir()
{
    return draxul::executable_directory();
}

std::filesystem::path server_runtime_dir(const draxul::ParsedArgs& parsed)
{
    if (!parsed.server_runtime_dir.empty())
        return parsed.server_runtime_dir;
    return draxul::server_runtime_directory(
        draxul::ConfigDocument::default_path().parent_path());
}

std::filesystem::path executable_path(
    const std::vector<std::string>& args)
{
    std::error_code path_error;
    if (!args.empty() && !args.front().empty())
    {
        auto path = std::filesystem::absolute(args.front(), path_error);
        if (!path_error && std::filesystem::exists(path))
            return path;
    }
#ifdef _WIN32
    constexpr std::string_view executable_name = "draxul.exe";
#else
    constexpr std::string_view executable_name = "draxul";
#endif
    return executable_dir() / executable_name;
}

// SIGTERM/SIGINT (and the Windows console-close events) request a GRACEFUL
// server stop: checkpoint sessions within the shutdown budget, then exit.
// Policy decided 2026-08-03: signals terminate without a confirmation dialog;
// the interactive quit menu keeps its query when live terminals exist. The
// handler only stores an atomic — the pump loop below acts on it — so no
// async-signal-unsafe work happens in signal context.
std::atomic<int> g_server_termination_signal{ 0 };

#ifdef _WIN32
BOOL WINAPI on_server_console_event(DWORD event)
{
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT
        || event == CTRL_CLOSE_EVENT || event == CTRL_SHUTDOWN_EVENT)
    {
        g_server_termination_signal.store(static_cast<int>(event) + 1);
        return TRUE;
    }
    return FALSE;
}
#else
extern "C" void on_server_termination_signal(int signal_number)
{
    g_server_termination_signal.store(signal_number);
}
#endif

void install_server_termination_handlers()
{
#ifdef _WIN32
    SetConsoleCtrlHandler(on_server_console_event, TRUE);
#else
    std::signal(SIGTERM, on_server_termination_signal);
    std::signal(SIGINT, on_server_termination_signal);
#endif
}

int report_server_startup_failure(
    const std::string& message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
        "Draxul server unavailable", message.c_str(), nullptr);
    draxul::shutdown_logging();
    return 1;
}

std::optional<std::string> create_plugin_launch_tab(
    const std::filesystem::path& runtime_directory,
    const std::string& client_id, const std::string& session_id,
    const std::shared_ptr<draxul::ClientRecoveryState>& recovery,
    std::string_view plugin_id, std::string_view plugin_config_json,
    std::string_view display_name, std::string& error)
{
    draxul::TopologyClient client({
        .runtime_directory = runtime_directory,
        .client_id = client_id,
        .session_id = session_id,
        .recovery = recovery,
    });
    if (!client.refresh(error))
        return std::nullopt;
    if (client.snapshot().spaces.empty())
    {
        error = "The server Session has no Space for the plugin tab.";
        return std::nullopt;
    }

    draxul::TopologyCommand command{
        .client_id = client_id,
        .command_id = draxul::make_server_client_id(),
        .kind = draxul::TopologyCommandKind::CreateTab,
        .space_id = client.snapshot().spaces.front().space_id,
        .name = display_name.empty() ? std::string(plugin_id)
                                     : std::string(display_name),
        .pane_domain = draxul::TopologyPaneDomain::ClientLocal,
        .client_host_kind = "plugin",
        .client_plugin_id = std::string(plugin_id),
        .client_plugin_config_json = plugin_config_json.empty()
            ? "{}"
            : std::string(plugin_config_json),
    };
    draxul::TopologyCommandResult result;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        command.expected_revision = client.snapshot().revision;
        if (client.execute(command, result, error))
        {
            if (!result.applied || result.created_id.empty())
            {
                error = "The server did not create the requested plugin tab.";
                return std::nullopt;
            }
            return result.created_id;
        }
        if (client.last_error_code() != "revision_conflict"
            || !client.refresh(error))
            break;
    }
    return std::nullopt;
}

int run_server_mode(const draxul::ParsedArgs& parsed,
    const std::filesystem::path& current_executable)
{
    const auto runtime_dir = server_runtime_dir(parsed);
    if (parsed.server_stop_dialog)
        return draxul::run_server_stop_dialog(runtime_dir);
    if (parsed.server)
    {
        const auto server_log
            = draxul::default_server_log_path(runtime_dir);
        // The log lives inside the runtime directory, which does not exist on
        // a first run — and a startup failure recorded nowhere is exactly the
        // debugging hole this log is for. Create it before configuring
        // logging; the kernel tightens permissions when it starts.
        std::error_code runtime_dir_error;
        std::filesystem::create_directories(
            runtime_dir, runtime_dir_error);
        const std::string server_log_text = server_log.string();
        draxul::configure_default_logging(
            server_log_text.c_str(), true);
        const draxul::AppConfig config
            = draxul::AppConfig::load();
        std::vector<draxul::AgentDefinition>
            agent_definitions;
        agent_definitions.reserve(
            config.agent_profiles.size());
        for (const draxul::AgentProfileConfig& profile : config.agent_profiles)
        {
            agent_definitions.push_back({
                .profile_id = profile.id,
                .kind = profile.kind,
                .display_name = profile.display_name,
                .executable = profile.executable,
                .default_args = profile.args,
                .restore_policy
                = draxul::parse_agent_restore_policy(
                    profile.restore_policy)
                    .value_or(
                        draxul::AgentRestorePolicy::
                            ResumeIfAvailable),
            });
        }
        draxul::ServerKernel kernel({
            .runtime_directory = runtime_dir,
            .protocol_major = draxul::kServerProtocolMajor,
            .protocol_minor = draxul::kServerProtocolMinor,
            .build_version = draxul::server_build_version(),
            .terminal_shell_kind = parsed.server_shell_kind,
            .terminal_command = parsed.server_command,
            .terminal_working_directory
            = parsed.server_working_dir.string(),
            .terminal_scrollback_lines
            = parsed.server_scrollback_lines,
            .agent_definitions
            = std::move(agent_definitions),
            .agents_resume_on_restore
            = config.agents_resume_on_restore,
        });
        const auto result = kernel.start();
        if (result.disposition == draxul::ServerStartDisposition::AlreadyRunning)
        {
            draxul::shutdown_logging();
            return 0;
        }
        if (result.disposition == draxul::ServerStartDisposition::Failed)
        {
            // stderr is /dev/null when the client launched us detached, so the
            // log file (already configured above) is the reachable record.
            DRAXUL_LOG_ERROR(draxul::LogCategory::App,
                "Draxul server failed to start: %s", result.error.c_str());
            std::fprintf(stderr, "Draxul server failed to start: %s\n",
                result.error.c_str());
            draxul::shutdown_logging();
            return 1;
        }
        install_server_termination_handlers();
        std::atomic<int> status{ 1 };
        std::jthread server_thread([&]() {
            status.store(kernel.run_until_stopped());
        });
        draxul::ServerStatusSurface surface({
            .runtime_directory = runtime_dir,
#ifdef __APPLE__
            .executable_path
            = draxul::macos_client_executable(
                current_executable),
#elif defined(_WIN32)
            .executable_path
            = draxul::windows_client_executable(
                current_executable),
#else
            .executable_path = current_executable,
#endif
            .log_path = server_log,
        });
        std::string surface_error;
        if (!surface.initialize(surface_error))
        {
            DRAXUL_LOG_WARN(draxul::LogCategory::App,
                "Draxul server status surface is unavailable; continuing headless: %s",
                surface_error.c_str());
        }
        while (kernel.running())
        {
            if (const int signal_number
                = g_server_termination_signal.exchange(0))
            {
                DRAXUL_LOG_INFO(draxul::LogCategory::App,
                    "Draxul server received termination signal %d; "
                    "stopping gracefully",
                    signal_number);
                kernel.request_stop();
            }
            if (surface.available())
                surface.pump();
            else
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(50));
            }
        }
        server_thread.join();
        surface.shutdown();
        draxul::shutdown_logging();
        return status.load();
    }

    if (parsed.server_status || parsed.list_sessions)
    {
        const auto result = draxul::ServerClient::status(runtime_dir);
        if (!result.ok || !result.status)
        {
            std::fprintf(stderr, "Draxul server is unavailable: %s\n",
                result.error_message.c_str());
            return 1;
        }
        if (parsed.list_sessions)
        {
            if (parsed.json_output)
            {
                const auto status_json
                    = draxul::server_status_to_json(*result.status);
                std::printf("%s\n",
                    status_json.at("session_statuses").dump().c_str());
            }
            else if (result.status->session_statuses.empty())
            {
                std::printf("No server Sessions.\n");
            }
            else
            {
                std::printf("Shared server Session store:\n");
                std::printf("%s",
                    draxul::format_server_session_listing_table(
                        result.status->session_statuses)
                        .c_str());
            }
        }
        else if (parsed.json_output)
        {
            std::printf("%s\n",
                draxul::server_status_to_json(*result.status).dump().c_str());
        }
        else
        {
            std::printf(
                "Draxul server: %s\nPID: %llu\nEpoch: %s\nProtocol: %d.%d\n"
                "Clients: %zu\nSessions: %zu\nSpaces: %zu\nTerminals: %zu\n"
                "Agents: %zu\nScrollback cells: %zu / %zu\n"
                "Checkpoint: %s\nCheckpoint path: %s\n",
                result.status->state.c_str(),
                static_cast<unsigned long long>(result.status->server_pid),
                result.status->server_epoch.c_str(),
                result.status->protocol_major,
                result.status->protocol_minor,
                result.status->connected_clients,
                result.status->sessions,
                result.status->spaces,
                result.status->terminals,
                result.status->agents,
                result.status->scrollback_cells_reserved,
                result.status->scrollback_cells_limit,
                result.status->checkpoint_state.c_str(),
                result.status->checkpoint_path.c_str());
            if (!result.status->checkpoint_error.empty())
            {
                std::printf("Checkpoint error: %s\n",
                    result.status->checkpoint_error.c_str());
            }
            for (const auto& warning : result.status->restore_warnings)
            {
                std::printf("Restore warning: %s\n",
                    warning.c_str());
            }
            for (const auto& session : result.status->session_statuses)
            {
                std::printf(
                    "Session %s: Spaces=%zu Terminals=%zu Live=%zu "
                    "Checkpoint=%s\n  Path: %s\n",
                    session.session_id.c_str(),
                    session.spaces,
                    session.terminals,
                    session.live_terminals,
                    session.checkpoint_state.c_str(),
                    session.checkpoint_path.c_str());
                if (!session.checkpoint_error.empty())
                {
                    std::printf("  Checkpoint error: %s\n",
                        session.checkpoint_error.c_str());
                }
                for (const auto& warning : session.restore_warnings)
                {
                    std::printf("  Restore warning: %s\n",
                        warning.c_str());
                }
            }
        }
        return 0;
    }

    std::string error;
    if (parsed.rename_session)
    {
        if (!draxul::ServerClient::rename_session(
                runtime_dir, parsed.session_id,
                parsed.session_name, error))
        {
            std::fprintf(stderr,
                "Could not rename shared server Session '%s': %s\n",
                parsed.session_id.c_str(), error.c_str());
            return 1;
        }
        std::printf(
            "Renamed shared server Session '%s' to '%s'.\n",
            parsed.session_id.c_str(),
            parsed.session_name.c_str());
        return 0;
    }
    if (parsed.delete_session)
    {
        if (!draxul::ServerClient::delete_session(
                runtime_dir, parsed.session_id,
                {
                    .confirm_live_terminals
                    = parsed.confirmed,
                },
                error))
        {
            std::fprintf(stderr,
                "Could not delete server Session '%s': %s\n",
                parsed.session_id.c_str(), error.c_str());
            return 1;
        }
        std::printf("Deleted shared server Session '%s'.\n",
            parsed.session_id.c_str());
        return 0;
    }
    if (parsed.delete_all_sessions)
    {
        if (!draxul::ServerClient::delete_all_sessions(
                runtime_dir,
                {
                    .confirm_live_terminals
                    = parsed.confirmed,
                },
                error))
        {
            std::fprintf(stderr,
                "Could not delete all server Sessions: %s\n",
                error.c_str());
            return 1;
        }
        std::printf("Deleted all shared server Sessions.\n");
        return 0;
    }
    if (parsed.force_stop_server)
    {
        if (!draxul::ServerClient::force_stop(
                runtime_dir, parsed.confirmed, error))
        {
            std::fprintf(stderr,
                "Could not force stop the Draxul server: %s\n",
                error.c_str());
            return 1;
        }
        std::printf("Draxul server force stopped.\n");
        return 0;
    }
    if (!draxul::ServerClient::shutdown(runtime_dir,
            { .confirm_live_terminals = parsed.confirmed }, error))
    {
        std::fprintf(stderr, "Could not stop the Draxul server: %s\n",
            error.c_str());
        return 1;
    }
    std::printf("Draxul server shutdown requested.\n");
    return 0;
}

} // namespace

static int draxul_main(std::vector<std::string> args)
{
    PERF_MEASURE();

    // Subcommands have their own option grammars. Dispatch them before the
    // launch/server flag parser so nouns such as "agent" are not rejected as
    // unknown top-level options.
    const auto integration_cli
        = draxul::parse_integration_cli(args);
#ifdef _WIN32
    if (integration_cli.recognized)
        ensure_console_io(true);
#endif
    if (integration_cli.error)
    {
        std::fprintf(
            stderr, "%s\n", integration_cli.error->c_str());
        return 1;
    }
    if (integration_cli.command)
        return draxul::run_integration_cli(
            *integration_cli.command);

    const auto topology_cli
        = draxul::parse_topology_cli(args);
#ifdef _WIN32
    if (topology_cli.recognized)
        ensure_console_io(true);
#endif
    if (topology_cli.error)
    {
        std::fprintf(
            stderr, "%s\n", topology_cli.error->c_str());
        return 2;
    }
    if (topology_cli.command)
        return draxul::run_topology_cli(
            *topology_cli.command);

    const auto control_cli = draxul::parse_control_cli(args);
#ifdef _WIN32
    if (control_cli.recognized)
        ensure_console_io(true);
#endif
    if (control_cli.error)
    {
        std::fprintf(
            stderr, "%s\n", control_cli.error->c_str());
        return 1;
    }
    if (control_cli.command)
        return draxul::run_control_cli(*control_cli.command);

    auto parse_result = draxul::parse_args(args);
#ifdef _WIN32
    const bool needs_console_output = parse_result.error.has_value()
        || parse_result.args.help
        || parse_result.args.want_console
        || parse_result.args.list_sessions
        || parse_result.args.rename_session
        || parse_result.args.delete_session
        || parse_result.args.delete_all_sessions
        || parse_result.args.server_status
        || parse_result.args.shutdown_server
        || parse_result.args.force_stop_server;
    if (needs_console_output)
        ensure_console_io(true);
#endif
    if (parse_result.error)
    {
        std::fprintf(stderr, "%s\n", parse_result.error->c_str());
        return 1;
    }
    draxul::ParsedArgs& parsed = parse_result.args;
    if (parsed.help)
    {
        std::fputs(draxul::cli_help_text(), stdout);
        return 0;
    }
    const auto current_executable = executable_path(args);
#ifdef __APPLE__
    if (parsed.server)
    {
        const auto server_executable
            = draxul::macos_server_helper_executable(
                current_executable);
        if (server_executable != current_executable)
        {
            if (!std::filesystem::exists(server_executable))
            {
                std::fprintf(stderr,
                    "Draxul server helper is unavailable: %s\n",
                    server_executable.string().c_str());
                return 1;
            }
            std::string server_executable_text
                = server_executable.string();
            std::vector<char*> server_argv;
            server_argv.reserve(args.size() + 1);
            for (std::string& argument : args)
                server_argv.push_back(argument.data());
            server_argv.front()
                = server_executable_text.data();
            server_argv.push_back(nullptr);
            ::execv(server_executable_text.c_str(),
                server_argv.data());
            std::fprintf(stderr,
                "Could not start the Draxul server helper.\n");
            return 1;
        }
    }
#endif
#ifdef _WIN32
    // Never leave the long-lived server inside draxul.exe: Windows prevents
    // the linker from replacing a running image during ordinary UI rebuilds.
    if (parsed.server
        && draxul::windows_server_helper_executable(
               current_executable)
            != current_executable)
    {
        std::string launch_error;
        if (!draxul::ServerClient::launch_detached({
                                                       .runtime_directory
                                                       = server_runtime_dir(parsed),
                                                       .executable_path = current_executable,
                                                       .terminal_shell_kind
                                                       = parsed.server_shell_kind,
                                                       .terminal_command
                                                       = parsed.server_command,
                                                       .terminal_working_directory
                                                       = parsed.server_working_dir,
                                                       .terminal_scrollback_lines
                                                       = parsed.server_scrollback_lines,
                                                   },
                launch_error))
        {
            std::fprintf(stderr,
                "Could not start the Draxul server helper: %s\n",
                launch_error.c_str());
            return 1;
        }
        return 0;
    }
#endif
    if (parsed.server || parsed.server_status
        || parsed.list_sessions
        || parsed.rename_session
        || parsed.delete_session
        || parsed.delete_all_sessions
        || parsed.shutdown_server
        || parsed.force_stop_server
        || parsed.server_stop_dialog)
    {
        return run_server_mode(parsed, current_executable);
    }

    const bool shared_server
        = draxul::should_use_shared_server(parsed);

    draxul::configure_default_logging();

    // Register core host providers. External products are resolved only by
    // stable plugin ID through the generic PluginHost provider.
    auto& host_registry = draxul::HostProviderRegistry::global();
    host_registry.clear();
    draxul::register_builtin_host_providers(host_registry);
    const auto plugin_manager = draxul::PluginManager::discover_default();
    draxul::register_plugin_host_provider(host_registry, plugin_manager);
    if (shared_server)
    {
        draxul::register_server_shell_host_metadata(
            host_registry);
    }
    draxul::register_nanovg_demo_host_provider(host_registry);
    draxul::markdown::register_markdown_host_provider(host_registry);
    draxul::kanban::register_kanban_host_provider(host_registry);

    if (const auto host_error = draxul::validate_host_provider_availability(parsed, host_registry))
    {
        std::fprintf(stderr, "%s\n", host_error->c_str());
        draxul::shutdown_logging();
        return 1;
    }

    // CLI overrides for logging — these always work, unlike env vars which
    // may not propagate to macOS .app bundles.
    if (!parsed.log_file.empty() || !parsed.log_level.empty())
    {
        draxul::LogOptions log_overrides;
        log_overrides.min_level = parsed.log_level.empty()
            ? draxul::LogLevel::Debug
            : draxul::parse_log_level_or(parsed.log_level, draxul::LogLevel::Debug);
        log_overrides.enable_stderr = true;
        log_overrides.enable_file = !parsed.log_file.empty();
        log_overrides.file_path = parsed.log_file;
        draxul::configure_logging(log_overrides);
    }

    std::optional<draxul::ServerWelcome> server_connection;
    std::filesystem::path connected_server_runtime;
    std::string connected_server_client_id;
    std::string launched_plugin_tab_id;
    std::shared_ptr<draxul::ClientRecoveryState>
        connected_client_recovery;
    const bool fake_remote_terminal
        = parsed.experimental_remote_terminal;
    const bool real_remote_terminal
        = shared_server && !fake_remote_terminal;
    if (shared_server)
    {
        connected_server_runtime = server_runtime_dir(parsed);
        connected_server_client_id = draxul::make_server_client_id();
        connected_client_recovery
            = std::make_shared<draxul::ClientRecoveryState>(
                connected_server_client_id);
        std::string selected_server_shell
            = parsed.server_shell_kind;
        if (selected_server_shell.empty()
            && parsed.host_kind
            && draxul::is_server_owned_shell_host(
                *parsed.host_kind)
            && *parsed.host_kind
                != draxul::HostKind::RemoteTerminal)
        {
            selected_server_shell
                = draxul::to_string(*parsed.host_kind);
        }
        draxul::ServerEnsureOptions server_options{
            .runtime_directory = connected_server_runtime,
#ifdef __APPLE__
            .executable_path
            = draxul::macos_server_helper_executable(
                current_executable),
#else
            .executable_path = current_executable,
#endif
            .client_id = connected_server_client_id,
            .registration_nonce
            = connected_client_recovery->registration_nonce(),
            .timeout = std::chrono::seconds(2),
            .terminal_shell_kind = selected_server_shell,
            .terminal_command = parsed.host_command,
            .terminal_working_directory
            = parsed.server_working_dir.empty()
                ? std::filesystem::current_path()
                : parsed.server_working_dir,
            .terminal_scrollback_lines
            = parsed.server_scrollback_lines,
        };
        auto server_result = draxul::ServerClient::ensure(server_options);
        if (!server_result.ready())
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
            return report_server_startup_failure(
                "Could not connect to the Draxul server ("
                + std::string(
                    draxul::to_string(server_result.state))
                + "): " + server_result.error_message + remediation);
        }
        if (fake_remote_terminal
            && std::ranges::find(server_result.welcome->capabilities,
                   "fake-remote-terminal")
                == server_result.welcome->capabilities.end())
        {
            return report_server_startup_failure(
                "The running Draxul server does not support "
                "the diagnostic fake terminal. Stop it and retry.");
        }
        if (real_remote_terminal
            && std::ranges::find(server_result.welcome->capabilities,
                   "real-remote-terminal")
                == server_result.welcome->capabilities.end())
        {
            return report_server_startup_failure(
                "The running Draxul server does not support "
                "shared shells. Stop it and retry.");
        }
        if (real_remote_terminal
            && std::ranges::find(server_result.welcome->capabilities,
                   "topology-v1")
                == server_result.welcome->capabilities.end())
        {
            return report_server_startup_failure(
                "The running Draxul server does not support "
                "shared topology. Stop it and retry.");
        }
        if (real_remote_terminal
            && std::ranges::find(server_result.welcome->capabilities,
                   "multi-terminal-v1")
                == server_result.welcome->capabilities.end())
        {
            return report_server_startup_failure(
                "The running Draxul server does not support "
                "multiple shared terminals. Stop it and retry.");
        }
        if (real_remote_terminal
            && parsed.session_id != "default"
            && std::ranges::find(server_result.welcome->capabilities,
                   "named-sessions-v1")
                == server_result.welcome->capabilities.end())
        {
            return report_server_startup_failure(
                "The running Draxul server does not support "
                "named Sessions. Stop it and retry.");
        }
        if (real_remote_terminal
            && std::ranges::find(
                   server_result.welcome->capabilities,
                   "agent-projection-v1")
                == server_result.welcome->capabilities.end())
        {
            return report_server_startup_failure(
                "The running Draxul server does not support "
                "shared Agents. Stop it and retry.");
        }
        if (real_remote_terminal
            && std::ranges::find(
                   server_result.welcome->capabilities,
                   "agent-control-v1")
                == server_result.welcome->capabilities.end())
        {
            return report_server_startup_failure(
                "The running Draxul server does not support "
                "headless Agent control. Stop it and retry.");
        }
        if (real_remote_terminal
            && std::ranges::find(
                   server_result.welcome->capabilities,
                   "managed-agent-v1")
                == server_result.welcome->capabilities.end())
        {
            return report_server_startup_failure(
                "The running Draxul server does not support "
                "managed Agents. Stop it and retry.");
        }
        if (!parsed.plugin_id.empty()
            && std::ranges::find(
                   server_result.welcome->capabilities,
                   "client-plugin-pane-v1")
                == server_result.welcome->capabilities.end())
        {
            return report_server_startup_failure(
                "The running Draxul server does not support plugin panes. "
                "Stop it and retry with this Draxul build.");
        }
        server_connection = std::move(server_result.welcome);
        connected_client_recovery->set_server_identity(
            server_connection->server_epoch,
            server_connection->connection_token);

        if (parsed.new_session)
        {
            const auto status = draxul::ServerClient::status(
                connected_server_runtime);
            if (!status.ok || !status.status)
            {
                return report_server_startup_failure(
                    "Could not inspect existing server Sessions: "
                    + status.error_message);
            }
            const auto session_exists
                = [&status](std::string_view id) {
                      return std::ranges::any_of(
                          status.status->session_statuses,
                          [id](const auto& session) {
                              return session.session_id == id;
                          });
                  };
            if (parsed.session_id_explicit)
            {
                if (session_exists(parsed.session_id))
                {
                    return report_server_startup_failure(
                        "Session '" + parsed.session_id
                        + "' already exists. Choose another id.");
                }
            }
            else
            {
                const int64_t unix_seconds
                    = std::chrono::duration_cast<
                        std::chrono::seconds>(
                        std::chrono::system_clock::now()
                            .time_since_epoch())
                          .count();
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

            draxul::TopologyClient creator({
                .runtime_directory = connected_server_runtime,
                .client_id = connected_server_client_id,
                .session_id = parsed.session_id,
                .recovery = connected_client_recovery,
            });
            std::string create_error;
            if (!creator.refresh(create_error))
            {
                return report_server_startup_failure(
                    "Could not create Session '"
                    + parsed.session_id + "': "
                    + create_error);
            }
            if (!parsed.session_name.empty()
                && !draxul::ServerClient::rename_session(
                    connected_server_runtime,
                    parsed.session_id,
                    parsed.session_name,
                    create_error))
            {
                return report_server_startup_failure(
                    "Created Session '" + parsed.session_id
                    + "' but could not set its name: "
                    + create_error);
            }
        }

        if (!parsed.plugin_id.empty())
        {
            const auto* manifest = plugin_manager->find(parsed.plugin_id);
            std::string launch_error;
            auto created = create_plugin_launch_tab(
                connected_server_runtime, connected_server_client_id,
                parsed.session_id, connected_client_recovery,
                parsed.plugin_id, parsed.plugin_config_json,
                manifest ? std::string_view(manifest->name)
                         : std::string_view(parsed.plugin_id),
                launch_error);
            if (!created)
            {
                return report_server_startup_failure(
                    "Could not create the plugin tab in the shared Session: "
                    + launch_error);
            }
            launched_plugin_tab_id = std::move(*created);
        }
    }

#ifdef DRAXUL_ENABLE_RENDER_TESTS
    std::optional<draxul::RenderTestScenario> render_test;
#endif
    draxul::AppOptions options;
    options.server_connection = std::move(server_connection);
    options.enable_remote_topology = real_remote_terminal;
    options.server_runtime_directory = connected_server_runtime;
    options.server_client_id = connected_server_client_id;
    options.startup_remote_tab_id = launched_plugin_tab_id;
    if (shared_server)
    {
        options.client_recovery = connected_client_recovery;
        if (options.server_connection)
        {
            options.client_recovery->set_server_identity(
                options.server_connection->server_epoch,
                options.server_connection->connection_token);
        }
    }
#ifdef DRAXUL_ENABLE_RENDER_TESTS
    if (!parsed.render_test_path.empty())
    {
        std::string load_error;
        render_test = draxul::load_render_test_scenario(parsed.render_test_path, &load_error);
        if (!render_test)
        {
            DRAXUL_LOG_ERROR(draxul::LogCategory::App, "%s", load_error.c_str());
            draxul::shutdown_logging();
            return 1;
        }
        options = render_test->make_app_options();
        options.show_render_test_window = parsed.show_render_test_window;
    }
    else if (parsed.smoke_test)
#else
    if (parsed.smoke_test)
#endif
    {
        options.activate_window_on_startup = false;
    }
    if (parsed.host_kind)
    {
        options.host_kind = *parsed.host_kind;
        options.host_kind_explicit = true;
    }
    if (!parsed.plugin_id.empty())
    {
        options.host_plugin_id = parsed.plugin_id;
        options.host_plugin_config_json = parsed.plugin_config_json;
    }
    if (!parsed.host_command.empty())
        options.host_command = parsed.host_command;
    if (!parsed.host_source_path.empty())
        options.host_source_path = parsed.host_source_path.string();
    if (!parsed.pty_capture_file.empty())
        options.pty_capture_file = parsed.pty_capture_file;
    if (parsed.continuous_refresh)
        options.request_continuous_refresh = true;
    if (parsed.no_vblank)
        options.no_vblank = true;
    if (parsed.no_ui)
        options.hide_host_ui_panels = true;
    if (shared_server)
    {
        options.host_kind = draxul::HostKind::RemoteTerminal;
        options.host_kind_explicit = true;
        if (!real_remote_terminal)
        {
            draxul::RemoteTerminalHostOptions remote_options{
                .runtime_directory = connected_server_runtime,
                .client_id = connected_server_client_id,
                .session_id = parsed.session_id,
                .server_epoch = options.server_connection
                    ? options.server_connection->server_epoch
                    : std::string{},
                .method_prefix = "fake",
                .recovery = options.client_recovery,
                .presentation_suspend_supported
                = options.server_connection
                    && std::ranges::find(
                           options.server_connection->capabilities,
                           "terminal-presentation-suspend-v1")
                        != options.server_connection->capabilities.end(),
            };
            options.host_factory = [remote_options](
                                       draxul::HostKind kind) {
                if (kind == draxul::HostKind::RemoteTerminal)
                {
                    return std::unique_ptr<draxul::IHost>(
                        std::make_unique<draxul::RemoteTerminalHost>(
                            remote_options));
                }
                return draxul::create_host(kind);
            };
        }
    }
    if (!parsed.screenshot_path.empty())
    {
        if (parsed.screenshot_width > 0 && parsed.screenshot_height > 0)
        {
            options.config_overrides.window_width = parsed.screenshot_width;
            options.config_overrides.window_height = parsed.screenshot_height;
            options.render_target_pixel_width = parsed.screenshot_width;
            options.render_target_pixel_height = parsed.screenshot_height;
        }
        options.save_user_config = false;
    }

    const auto exe_dir = executable_dir();
    options.executable_path = current_executable;
    if (!options.config_overrides.font_path.has_value() && !exe_dir.empty())
    {
        const auto bundled_font = exe_dir / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
        if (std::filesystem::exists(bundled_font))
            options.config_overrides.font_path = bundled_font.string();
    }
    options.session_id = parsed.session_id;
    options.control_id = shared_server
        ? connected_server_client_id
        : parsed.session_id;
    if (!parsed.session_name.empty())
        options.session_name = parsed.session_name;
    options.new_session_requested = parsed.new_session;
#ifdef DRAXUL_ENABLE_RENDER_TESTS
    const bool allow_session_restore = !parsed.smoke_test
        && !render_test.has_value()
        && parsed.screenshot_path.empty();
#else
    const bool allow_session_restore = !parsed.smoke_test
        && parsed.screenshot_path.empty();
#endif
    options.enable_session_restore
        = allow_session_restore && !shared_server;
    options.enable_control_server = allow_session_restore;

    draxul::App app(std::move(options));

    if (!app.initialize())
    {
        DRAXUL_LOG_ERROR(draxul::LogCategory::App, "Failed to initialize draxul: %s", app.init_error().c_str());
#ifdef DRAXUL_ENABLE_RENDER_TESTS
        const bool ci_mode = parsed.smoke_test || !parsed.render_test_path.empty() || !parsed.screenshot_path.empty();
#else
        const bool ci_mode = parsed.smoke_test || !parsed.screenshot_path.empty();
#endif
        if (!ci_mode)
        {
            const std::string& reason = app.init_error();
            const char* message = reason.empty()
                ? "Draxul failed to start. Check the log for details."
                : reason.c_str();
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Draxul \xe2\x80\x94 Startup Error", message, nullptr);
        }
        draxul::shutdown_logging();
        return 1;
    }

    int status = 0;
#ifdef DRAXUL_ENABLE_RENDER_TESTS
    if (render_test)
    {
        auto frame = app.run_render_test(std::chrono::milliseconds(render_test->timeout_ms),
            std::chrono::milliseconds(render_test->settle_ms));
        if (!frame)
        {
            draxul::write_render_test_failure_report(*render_test, app.last_render_test_error());
            status = 1;
        }
        else
        {
            std::string finalize_error;
            bool ok = false;
            if (!parsed.export_render_test_path.empty())
                ok = draxul::export_render_test_frame(parsed.export_render_test_path, *frame, &finalize_error);
            else
                ok = draxul::finalize_render_test_result(*render_test, *frame, parsed.bless_render_test, &finalize_error);

            if (!ok)
            {
                DRAXUL_LOG_ERROR(draxul::LogCategory::Test, "Render test finalize failed: %s",
                    finalize_error.c_str());
                draxul::write_render_test_failure_report(*render_test, finalize_error);
                DRAXUL_LOG_ERROR(draxul::LogCategory::App, "%s", finalize_error.c_str());
                status = 1;
            }
        }
    }
    else if (parsed.smoke_test)
#else
    if (parsed.smoke_test)
#endif
    {
        if (!app.run_smoke_test(std::chrono::seconds(3)))
            status = 1;
    }
    else if (!parsed.screenshot_path.empty())
    {
        if (!parsed.gui_action.empty())
        {
            // Test hook: pump until content is ready, fire the action, then
            // let the screenshot capture whatever it caused (e.g. a toast).
            app.run_smoke_test(std::chrono::seconds(10));
            if (!app.dispatch_gui_action(parsed.gui_action))
                DRAXUL_LOG_ERROR(draxul::LogCategory::App, "--gui-action '%s' was not recognized",
                    parsed.gui_action.c_str());
        }
        auto frame = app.run_screenshot(std::chrono::milliseconds(parsed.screenshot_delay_ms));
        if (frame)
        {
            if (!draxul::write_bmp_rgba(parsed.screenshot_path, *frame))
            {
                DRAXUL_LOG_ERROR(draxul::LogCategory::App, "Failed to write screenshot to %s",
                    parsed.screenshot_path.string().c_str());
                status = 1;
            }
        }
        else
        {
            DRAXUL_LOG_ERROR(draxul::LogCategory::App, "Failed to capture screenshot");
            status = 1;
        }
    }
    else
    {
        app.run();
    }
    app.shutdown();
    if (shared_server && connected_client_recovery)
    {
        const auto identity
            = connected_client_recovery->server_identity();
        std::string disconnect_error;
        if (!draxul::ServerClient::disconnect(
                connected_server_runtime,
                connected_server_client_id,
                disconnect_error,
                identity.connection_token))
        {
            DRAXUL_LOG_WARN(draxul::LogCategory::App,
                "Failed to release server client identity: %s",
                disconnect_error.c_str());
        }
    }
    draxul::shutdown_logging();

    return status;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    ensure_valid_standard_io();
    return draxul_main(command_line_args());
}
#else
int main(int argc, char* argv[])
{
    return draxul_main(command_line_args(argc, argv));
}
#endif
