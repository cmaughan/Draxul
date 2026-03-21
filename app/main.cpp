#include "app.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <draxul/log.h>
#include <draxul/nvim_rpc.h>
#ifdef DRAXUL_ENABLE_RENDER_TESTS
#include <draxul/render_test.h>
#endif
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <shellapi.h>
#include <windows.h>
#endif

namespace
{

#ifdef _WIN32
std::vector<std::string> command_line_args()
{
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
#ifdef DRAXUL_ENABLE_RENDER_TESTS
    bool bless_render_test = false;
    bool show_render_test_window = false;
    std::filesystem::path render_test_path;
    std::filesystem::path export_render_test_path;
#endif
    std::optional<draxul::HostKind> host_kind;
    std::string host_command;
};

ParsedArgs parse_args(const std::vector<std::string>& args)
{
    ParsedArgs parsed;
    for (size_t i = 1; i < args.size(); ++i)
    {
        if (args[i] == "--console")
            parsed.want_console = true;
        else if (args[i] == "--smoke-test")
            parsed.smoke_test = true;
#ifdef DRAXUL_ENABLE_RENDER_TESTS
        else if (args[i] == "--bless-render-test")
            parsed.bless_render_test = true;
        else if (args[i] == "--show-render-test-window")
            parsed.show_render_test_window = true;
        else if (args[i] == "--render-test" && i + 1 < args.size())
            parsed.render_test_path = args[++i];
        else if (args[i] == "--export-render-test" && i + 1 < args.size())
            parsed.export_render_test_path = args[++i];
#endif
        else if (args[i] == "--host" && i + 1 < args.size())
            parsed.host_kind = draxul::parse_host_kind(args[++i]);
        else if (args[i] == "--command" && i + 1 < args.size())
        {
            ++i;
            parsed.host_command = args[i];
        }
    }
    return parsed;
}

void update_current_directory(const std::vector<std::string>& args)
{
    if (args.empty())
        return;

    auto exe_path = std::filesystem::path(args.front()).parent_path();
    if (!exe_path.empty())
        std::filesystem::current_path(exe_path);
}

} // namespace

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    auto args = command_line_args();
#else
int main(int argc, char* argv[])
{
    auto args = command_line_args(argc, argv);
#endif
    ParsedArgs parsed = parse_args(args);

#ifdef _WIN32
    if (parsed.want_console)
    {
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif

    update_current_directory(args);

    draxul::configure_default_logging();

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

    draxul::App app(std::move(options));

    if (!app.initialize())
    {
        DRAXUL_LOG_ERROR(draxul::LogCategory::App, "Failed to initialize draxul");
#ifdef DRAXUL_ENABLE_RENDER_TESTS
        const bool ci_mode = parsed.smoke_test || !parsed.render_test_path.empty();
#else
        const bool ci_mode = parsed.smoke_test;
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

    draxul::set_main_thread_id(std::this_thread::get_id());

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
    else
    {
        app.run();
    }
    app.shutdown();
    draxul::shutdown_logging();

    return status;
}
