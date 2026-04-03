#include "app.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <draxul/bmp.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#ifdef DRAXUL_ENABLE_RENDER_TESTS
#include <draxul/render_test.h>
#endif
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// This must come after shellapi!
#include <shellapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace
{

#ifdef _WIN32
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
        std::string converted(size - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, converted.data(), static_cast<int>(converted.size()), nullptr, nullptr);
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

struct ParsedArgs
{
    bool want_console = false;
    bool smoke_test = false;
    bool continuous_refresh = false;
    bool no_vblank = false;
    bool no_ui = false;
#ifdef DRAXUL_ENABLE_RENDER_TESTS
    bool bless_render_test = false;
    bool show_render_test_window = false;
    std::filesystem::path render_test_path;
    std::filesystem::path export_render_test_path;
#endif
    std::optional<draxul::HostKind> host_kind;
    std::string host_command;
    std::string log_file;
    std::string log_level;
    std::filesystem::path screenshot_path;
    int screenshot_delay_ms = 6000;
    int screenshot_width = 0;
    int screenshot_height = 0;
};

ParsedArgs parse_args(const std::vector<std::string>& args)
{
    PERF_MEASURE();
    ParsedArgs parsed;
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
            parsed.host_kind = draxul::parse_host_kind(args[i]);
        }
        else if (args[i] == "--command" && i + 1 < args.size())
        {
            ++i;
            parsed.host_command = args[i];
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
        else if (args[i] == "--screenshot" && i + 1 < args.size())
        {
            ++i;
            parsed.screenshot_path = args[i];
        }
        else if (args[i] == "--screenshot-delay" && i + 1 < args.size())
        {
            ++i;
            try
            {
                int value = std::stoi(args[i]);
                if (value < 0)
                {
                    std::fprintf(stderr, "error: --screenshot-delay requires a non-negative integer\n");
                    std::exit(1);
                }
                parsed.screenshot_delay_ms = value;
            }
            catch (const std::exception&)
            {
                std::fprintf(stderr, "error: --screenshot-delay requires a non-negative integer\n");
                std::exit(1);
            }
        }
        else if (args[i] == "--screenshot-size" && i + 1 < args.size())
        {
            ++i;
            const auto& size_str = args[i];
            auto x_pos = size_str.find('x');
            if (x_pos != std::string::npos)
            {
                try
                {
                    parsed.screenshot_width = std::stoi(size_str.substr(0, x_pos));
                    parsed.screenshot_height = std::stoi(size_str.substr(x_pos + 1));
                    if (parsed.screenshot_width <= 0 || parsed.screenshot_height <= 0)
                    {
                        std::fprintf(stderr, "error: --screenshot-size requires positive dimensions (e.g. 800x600)\n");
                        std::exit(1);
                    }
                }
                catch (const std::exception&)
                {
                    std::fprintf(stderr, "error: --screenshot-size requires valid dimensions (e.g. 800x600)\n");
                    std::exit(1);
                }
            }
        }
    }
    return parsed;
}

std::filesystem::path executable_dir(const std::vector<std::string>& args)
{
    PERF_MEASURE();
#ifdef _WIN32
    std::wstring exe_path(MAX_PATH, L'\0');
    for (;;)
    {
        DWORD size = GetModuleFileNameW(nullptr, exe_path.data(), static_cast<DWORD>(exe_path.size()));
        if (size == 0)
            return {};
        if (size < exe_path.size())
        {
            exe_path.resize(size);
            break;
        }
        exe_path.resize(exe_path.size() * 2);
    }
    return std::filesystem::path(exe_path).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0)
        return {};

    std::string exe_path(size, '\0');
    if (_NSGetExecutablePath(exe_path.data(), &size) != 0)
        return {};
    exe_path.resize(std::char_traits<char>::length(exe_path.c_str()));
    return std::filesystem::path(exe_path).parent_path();
#elif defined(__linux__)
    std::vector<char> exe_path(256, '\0');
    for (;;)
    {
        const ssize_t size = readlink("/proc/self/exe", exe_path.data(), exe_path.size());
        if (size < 0)
            break;
        if (static_cast<size_t>(size) < exe_path.size())
            return std::filesystem::path(std::string(exe_path.data(), static_cast<size_t>(size))).parent_path();
        exe_path.resize(exe_path.size() * 2);
    }
    if (args.empty())
        return {};
    return std::filesystem::absolute(std::filesystem::path(args.front())).parent_path();
#else
    if (args.empty())
        return {};
    return std::filesystem::absolute(std::filesystem::path(args.front())).parent_path();
#endif
}

} // namespace

static int draxul_main(std::vector<std::string> args)
{
    PERF_MEASURE();
    ParsedArgs parsed = parse_args(args);

#ifdef _WIN32
    if (parsed.want_console)
    {
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif

    draxul::configure_default_logging();

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

#ifdef DRAXUL_ENABLE_RENDER_TESTS
    std::optional<draxul::RenderTestScenario> render_test;
#endif
    draxul::AppOptions options;
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
        options.host_kind = *parsed.host_kind;
    if (!parsed.host_command.empty())
        options.host_command = parsed.host_command;
    if (parsed.continuous_refresh)
        options.megacity_continuous_refresh = true;
    if (parsed.no_vblank)
        options.no_vblank = true;
    if (parsed.no_ui)
        options.no_ui = true;
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

    const auto exe_dir = executable_dir(args);
    if (!options.config_overrides.font_path.has_value() && !exe_dir.empty())
    {
        const auto bundled_font = exe_dir / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
        if (std::filesystem::exists(bundled_font))
            options.config_overrides.font_path = bundled_font.string();
    }

    draxul::App app(std::move(options));

    if (!app.initialize())
    {
        DRAXUL_LOG_ERROR(draxul::LogCategory::App, "Failed to initialize draxul");
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
