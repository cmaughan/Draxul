#include "agent_integration.h"
#include "app.h"
#include "cli_args.h"
#include "control_cli.h"
#include "session_cli.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <draxul/bmp.h>
#include <draxul/config_document.h>
#include <draxul/host_registry.h>
#include <draxul/kanban/kanban_host.h>
#include <draxul/log.h>
#include <draxul/markdown/markdown_host.h>
#include <draxul/nanovg_demo_host.h>
#include <draxul/perf_timing.h>
#include <draxul/remote_terminal_host.h>
#include <draxul/runtime_path.h>
#include <draxul/server_client.h>
#include <draxul/server_kernel.h>
#include <draxul/server_protocol.h>
#ifdef DRAXUL_ENABLE_MEGACITY
#include <draxul/megacity_host.h>
#endif
#ifdef DRAXUL_ENABLE_SATVIEW
#include <draxul/satview/satview_host.h>
#endif
#ifdef DRAXUL_ENABLE_SCOREVIEW
#include <draxul/scoreview/score_host.h>
#endif
#ifdef DRAXUL_ENABLE_RENDER_TESTS
#include <draxul/render_test.h>
#endif
#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
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
void ensure_console_io(bool allow_alloc_console)
{
    static bool configured = false;
    if (configured)
        return;

    if (!GetConsoleWindow())
    {
        if (!AttachConsole(ATTACH_PARENT_PROCESS) && allow_alloc_console)
            AllocConsole();
    }

    if (GetConsoleWindow())
    {
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
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

int run_server_mode(const draxul::ParsedArgs& parsed)
{
    const auto runtime_dir = server_runtime_dir(parsed);
    if (parsed.server)
    {
        draxul::configure_default_logging("draxul-server.log", true);
        draxul::ServerKernel kernel({
            .runtime_directory = runtime_dir,
            .protocol_major = draxul::kServerProtocolMajor,
            .protocol_minor = draxul::kServerProtocolMinor,
            .build_version = draxul::server_build_version(),
        });
        const auto result = kernel.start();
        if (result.disposition == draxul::ServerStartDisposition::AlreadyRunning)
        {
            draxul::shutdown_logging();
            return 0;
        }
        if (result.disposition == draxul::ServerStartDisposition::Failed)
        {
            std::fprintf(stderr, "Draxul server failed to start: %s\n",
                result.error.c_str());
            draxul::shutdown_logging();
            return 1;
        }
        const int status = kernel.run_until_stopped();
        draxul::shutdown_logging();
        return status;
    }

    if (parsed.server_status)
    {
        const auto result = draxul::ServerClient::status(runtime_dir);
        if (!result.ok || !result.status)
        {
            std::fprintf(stderr, "Draxul server is unavailable: %s\n",
                result.error_message.c_str());
            return 1;
        }
        if (parsed.json_output)
        {
            std::printf("%s\n",
                draxul::server_status_to_json(*result.status).dump().c_str());
        }
        else
        {
            std::printf(
                "Draxul server: %s\nPID: %llu\nEpoch: %s\nProtocol: %d.%d\n"
                "Clients: %zu\n",
                result.status->state.c_str(),
                static_cast<unsigned long long>(result.status->server_pid),
                result.status->server_epoch.c_str(),
                result.status->protocol_major,
                result.status->protocol_minor,
                result.status->connected_clients);
        }
        return 0;
    }

    std::string error;
    if (!draxul::ServerClient::shutdown(runtime_dir, error))
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
    auto parse_result = draxul::parse_args(args);
#ifdef _WIN32
    const bool needs_console_output = parse_result.error.has_value()
        || parse_result.args.want_console
        || parse_result.args.list_sessions
        || parse_result.args.rename_session
        || parse_result.args.delete_session
        || parse_result.args.server_status
        || parse_result.args.shutdown_server;
    if (needs_console_output)
        ensure_console_io(true);
#endif
    if (parse_result.error)
    {
        std::fprintf(stderr, "%s\n", parse_result.error->c_str());
        return 1;
    }
    draxul::ParsedArgs& parsed = parse_result.args;
    if (parsed.server || parsed.server_status || parsed.shutdown_server)
        return run_server_mode(parsed);

    const auto integration_cli = draxul::parse_integration_cli(args);
#ifdef _WIN32
    if (integration_cli.recognized)
        ensure_console_io(true);
#endif
    if (integration_cli.error)
    {
        std::fprintf(stderr, "%s\n", integration_cli.error->c_str());
        return 1;
    }
    if (integration_cli.command)
        return draxul::run_integration_cli(*integration_cli.command);

    const auto control_cli = draxul::parse_control_cli(args);
#ifdef _WIN32
    if (control_cli.recognized)
        ensure_console_io(true);
#endif
    if (control_cli.error)
    {
        std::fprintf(stderr, "%s\n", control_cli.error->c_str());
        return 1;
    }
    if (control_cli.command)
        return draxul::run_control_cli(*control_cli.command);

    draxul::SessionCli session_cli;

    std::string new_session_error;
    if (!session_cli.prepare_new_session_launch(parsed, &new_session_error))
    {
        // Refuse rather than silently downgrade. Falling back to "default"
        // handed the user the shared Session they had explicitly asked to
        // avoid — and "default" is the id most likely to already be open,
        // which is now a hard refusal at startup.
#ifdef _WIN32
        ensure_console_io(true);
#endif
        std::fprintf(stderr, "%s\n",
            new_session_error.empty() ? "Could not prepare the requested session."
                                      : new_session_error.c_str());
        return 1;
    }

    draxul::configure_default_logging();

    // Register host providers. The terminal product registers its built-ins
    // unconditionally; optional modules (megacity) only register when their
    // build flag is on. Nothing in draxul-app or draxul-host links the
    // megacity library — this main.cpp file is the sole megacity touchpoint
    // in the executable.
    auto& host_registry = draxul::HostProviderRegistry::global();
    host_registry.clear();
    draxul::register_builtin_host_providers(host_registry);
    draxul::register_nanovg_demo_host_provider(host_registry);
    draxul::markdown::register_markdown_host_provider(host_registry);
    draxul::kanban::register_kanban_host_provider(host_registry);
#ifdef DRAXUL_ENABLE_MEGACITY
    draxul::register_megacity_host_provider(host_registry);
#endif
#ifdef DRAXUL_ENABLE_SATVIEW
    draxul::satview::register_satview_host_provider(host_registry);
#endif
#ifdef DRAXUL_ENABLE_SCOREVIEW
    draxul::scoreview::register_score_host_provider(host_registry);
#endif

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

    const auto session_cli_result = session_cli.run(
        draxul::SessionCliRequest::from_parsed_args(parsed));
    if (session_cli_result.disposition != draxul::SessionCliDisposition::Continue)
    {
        if (!session_cli_result.output.empty())
            std::fputs(session_cli_result.output.c_str(), stdout);
        if (!session_cli_result.error.empty())
            std::fputs(session_cli_result.error.c_str(), stderr);
        draxul::shutdown_logging();
        return session_cli_result.disposition == draxul::SessionCliDisposition::Handled ? 0 : 1;
    }

    std::optional<draxul::ServerWelcome> server_connection;
    std::filesystem::path connected_server_runtime;
    std::string connected_server_client_id;
    if (draxul::should_bootstrap_experimental_server(parsed))
    {
#ifdef _WIN32
        ensure_console_io(true);
#endif
        connected_server_runtime = server_runtime_dir(parsed);
        connected_server_client_id = draxul::make_server_client_id();
        draxul::ServerEnsureOptions server_options{
            .runtime_directory = connected_server_runtime,
            .executable_path = executable_path(args),
            .client_id = connected_server_client_id,
        };
        auto server_result = draxul::ServerClient::ensure(server_options);
        if (!server_result.ready())
        {
            std::fprintf(stderr,
                "Could not connect to the experimental Draxul server (%s): %s\n",
                std::string(draxul::to_string(server_result.state)).c_str(),
                server_result.error_message.c_str());
            draxul::shutdown_logging();
            return 1;
        }
        if (parsed.experimental_remote_terminal
            && std::ranges::find(server_result.welcome->capabilities,
                   "fake-remote-terminal")
                == server_result.welcome->capabilities.end())
        {
            std::fprintf(stderr,
                "The running Draxul server does not support the experimental remote terminal. Stop it and retry.\n");
            draxul::shutdown_logging();
            return 1;
        }
        server_connection = std::move(server_result.welcome);
    }

#ifdef DRAXUL_ENABLE_RENDER_TESTS
    std::optional<draxul::RenderTestScenario> render_test;
#endif
    draxul::AppOptions options;
    options.server_connection = std::move(server_connection);
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
    if (parsed.experimental_remote_terminal)
    {
        options.host_kind = draxul::HostKind::RemoteTerminal;
        options.host_kind_explicit = true;
        draxul::RemoteTerminalHostOptions remote_options{
            .runtime_directory = connected_server_runtime,
            .client_id = connected_server_client_id,
            .server_epoch = options.server_connection
                ? options.server_connection->server_epoch
                : std::string{},
        };
        options.host_factory = [remote_options](draxul::HostKind kind) {
            if (kind == draxul::HostKind::RemoteTerminal)
            {
                return std::unique_ptr<draxul::IHost>(
                    std::make_unique<draxul::RemoteTerminalHost>(
                        remote_options));
            }
            return draxul::create_host(kind);
        };
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
    if (!options.config_overrides.font_path.has_value() && !exe_dir.empty())
    {
        const auto bundled_font = exe_dir / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
        if (std::filesystem::exists(bundled_font))
            options.config_overrides.font_path = bundled_font.string();
    }
    options.session_id = parsed.session_id;
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
        = allow_session_restore && !parsed.experimental_remote_terminal;
    options.enable_control_server
        = allow_session_restore && !parsed.experimental_remote_terminal;

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
    draxul::shutdown_logging();

    return status;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return draxul_main(command_line_args());
}
#else
int main(int argc, char* argv[])
{
    return draxul_main(command_line_args(argc, argv));
}
#endif
