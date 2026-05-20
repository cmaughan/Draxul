#include "app.h"
#include "cli_args.h"
#include "session_listing.h"
#include "session_picker_host.h"
#include "session_state.h"
#include <SDL3/SDL.h>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <draxul/bmp.h>
#include <draxul/host_registry.h>
#include <draxul/kanban/kanban_host.h>
#include <draxul/log.h>
#include <draxul/markdown/markdown_host.h>
#include <draxul/nanovg_demo_host.h>
#include <draxul/perf_timing.h>
#include <draxul/session_attach.h>
#ifdef DRAXUL_ENABLE_MEGACITY
#include <draxul/megacity_host.h>
#endif
#ifdef DRAXUL_ENABLE_RENDER_TESTS
#include <draxul/render_test.h>
#endif
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <cstdio>
#include <windows.h>

// This must come after shellapi!
#include <shellapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#elif defined(__linux__)
#include <unistd.h>
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

std::filesystem::path executable_path(const std::vector<std::string>& args)
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
    return std::filesystem::path(exe_path);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0)
        return {};

    std::string exe_path(size, '\0');
    if (_NSGetExecutablePath(exe_path.data(), &size) != 0)
        return {};
    exe_path.resize(std::char_traits<char>::length(exe_path.c_str()));
    return std::filesystem::path(exe_path);
#elif defined(__linux__)
    std::vector<char> exe_path(256, '\0');
    for (;;)
    {
        const ssize_t size = readlink("/proc/self/exe", exe_path.data(), exe_path.size());
        if (size < 0)
            break;
        if (static_cast<size_t>(size) < exe_path.size())
            return std::filesystem::path(std::string(exe_path.data(), static_cast<size_t>(size)));
        exe_path.resize(exe_path.size() * 2);
    }
    if (args.empty())
        return {};
    return std::filesystem::absolute(std::filesystem::path(args.front()));
#else
    if (args.empty())
        return {};
    return std::filesystem::absolute(std::filesystem::path(args.front()));
#endif
}

std::filesystem::path executable_dir(const std::vector<std::string>& args)
{
    const auto path = executable_path(args);
    return path.empty() ? path : path.parent_path();
}

bool rename_saved_session_records(
    std::string_view session_id, std::string_view session_name, std::string* error)
{
    bool updated = false;
    std::string io_error;
    if (auto state = draxul::load_session_state(session_id, &io_error))
    {
        state->session_name = std::string(session_name);
        if (!draxul::save_session_state(*state, &io_error))
        {
            if (error)
                *error = io_error;
            return false;
        }
        updated = true;
    }
    else if (!io_error.empty())
    {
        if (error)
            *error = io_error;
        return false;
    }

    io_error.clear();
    if (auto metadata = draxul::load_session_runtime_metadata(session_id, &io_error))
    {
        metadata->session_name = std::string(session_name);
        if (!draxul::save_session_runtime_metadata(*metadata, &io_error))
        {
            if (error)
                *error = io_error;
            return false;
        }
        updated = true;
    }
    else if (!io_error.empty())
    {
        if (error)
            *error = io_error;
        return false;
    }

    if (!updated && error)
        *error = "No running or saved session was found.";
    return updated;
}

std::string generated_session_slug(std::string_view text)
{
    std::string slug;
    slug.reserve(text.size());
    bool last_was_separator = false;
    for (unsigned char ch : text)
    {
        if (std::isalnum(ch))
        {
            slug.push_back(static_cast<char>(std::tolower(ch)));
            last_was_separator = false;
        }
        else if (!last_was_separator && !slug.empty())
        {
            slug.push_back('-');
            last_was_separator = true;
        }
    }

    while (!slug.empty() && slug.back() == '-')
        slug.pop_back();
    if (slug.empty())
        slug = "session";
    if (slug.size() > 40)
        slug.resize(40);
    return slug;
}

std::tm local_time_from_unix(int64_t unix_seconds)
{
    const std::time_t raw = static_cast<std::time_t>(unix_seconds);
    std::tm local = {};
#ifdef _WIN32
    localtime_s(&local, &raw);
#else
    localtime_r(&raw, &local);
#endif
    return local;
}

std::string generated_session_timestamp(int64_t unix_seconds)
{
    const std::tm local = local_time_from_unix(unix_seconds);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d%02d%02d-%02d%02d%02d",
        local.tm_year + 1900,
        local.tm_mon + 1,
        local.tm_mday,
        local.tm_hour,
        local.tm_min,
        local.tm_sec);
    return buffer;
}

bool session_exists(std::string_view session_id, std::string* error)
{
    std::string probe_error;
    const auto probe_status = draxul::SessionAttachServer::probe(session_id, &probe_error);
    if (probe_status == draxul::SessionAttachServer::ProbeStatus::Running)
        return true;
    if (probe_status == draxul::SessionAttachServer::ProbeStatus::Error)
    {
        if (error)
            *error = probe_error.empty()
                ? "Failed probing for an existing session."
                : probe_error;
        return false;
    }

    std::string io_error;
    if (draxul::has_saved_session_state(session_id, &io_error))
        return true;
    if (!io_error.empty())
    {
        if (error)
            *error = io_error;
        return false;
    }

    (void)draxul::clear_session_runtime_liveness(session_id);

    return false;
}

bool prepare_new_session_launch(draxul::ParsedArgs& parsed, std::string* error)
{
    if (!parsed.new_session)
        return true;

    if (parsed.session_id_explicit)
    {
        std::string exists_error;
        if (session_exists(parsed.session_id, &exists_error))
        {
            if (error)
                *error = "Session '" + parsed.session_id
                    + "' already exists. Use --session with a different id or omit it to generate one.";
            return false;
        }
        if (!exists_error.empty())
        {
            if (error)
                *error = exists_error;
            return false;
        }
        return true;
    }

    const int64_t unix_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
                                     .count();
    const std::string stem = generated_session_slug(parsed.session_name);
    const std::string candidate_base = stem + "-" + generated_session_timestamp(unix_seconds);
    std::string candidate = candidate_base;
    for (int suffix = 2;; ++suffix)
    {
        std::string exists_error;
        if (!session_exists(candidate, &exists_error))
        {
            if (!exists_error.empty())
            {
                if (error)
                    *error = exists_error;
                return false;
            }
            parsed.session_id = candidate;
            return true;
        }
        candidate = candidate_base + "-" + std::to_string(suffix);
    }
}

std::vector<std::string> session_owner_args(
    const std::vector<std::string>& args, const std::filesystem::path& exe_path, const draxul::ParsedArgs& parsed)
{
    std::vector<std::string> owner_args;
    owner_args.reserve(args.size() + 4);
    owner_args.emplace_back(exe_path.string());
    for (size_t i = 1; i < args.size(); ++i)
    {
        if (args[i] == "--console")
            continue;
        if (args[i] == "--session-owner")
            continue;
        if (args[i] == "--session" && i + 1 < args.size())
        {
            ++i;
            continue;
        }
        owner_args.push_back(args[i]);
    }
    owner_args.emplace_back("--session");
    owner_args.emplace_back(parsed.session_id);
    if (!parsed.session_name.empty())
    {
        owner_args.emplace_back("--session-name");
        owner_args.emplace_back(parsed.session_name);
    }
    owner_args.emplace_back("--session-owner");
    return owner_args;
}

#ifdef _WIN32
std::wstring widen_utf8(std::string_view text)
{
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string quote_windows_arg(std::string_view arg)
{
    const bool needs_quotes = arg.empty()
        || arg.find_first_of(" \t\n\v\"") != std::string_view::npos;
    if (!needs_quotes)
        return std::string(arg);

    std::string quoted;
    quoted.push_back('"');
    size_t pending_backslashes = 0;
    for (char ch : arg)
    {
        if (ch == '\\')
        {
            ++pending_backslashes;
            continue;
        }

        if (ch == '"')
            quoted.append(pending_backslashes * 2 + 1, '\\');
        else
            quoted.append(pending_backslashes, '\\');
        pending_backslashes = 0;
        quoted.push_back(ch);
    }
    quoted.append(pending_backslashes * 2, '\\');
    quoted.push_back('"');
    return quoted;
}
#endif

bool spawn_session_owner_process(
    const std::filesystem::path& exe_path, const std::vector<std::string>& owner_args, std::string* error)
{
    PERF_MEASURE();
#ifdef _WIN32
    std::string command_line_utf8;
    for (const auto& arg : owner_args)
    {
        if (!command_line_utf8.empty())
            command_line_utf8.push_back(' ');
        command_line_utf8 += quote_windows_arg(arg);
    }

    std::wstring exe_path_w = exe_path.wstring();
    std::wstring command_line = widen_utf8(command_line_utf8);
    std::vector<wchar_t> command_line_buffer(command_line.begin(), command_line.end());
    command_line_buffer.push_back(L'\0');

    STARTUPINFOW startup_info = {};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info = {};
    const BOOL ok = CreateProcessW(
        exe_path_w.c_str(),
        command_line_buffer.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startup_info,
        &process_info);
    if (!ok)
    {
        if (error)
            *error = "CreateProcessW failed: " + std::to_string(GetLastError());
        return false;
    }
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return true;
#else
    const pid_t child = fork();
    if (child < 0)
    {
        if (error)
            *error = "fork() failed";
        return false;
    }
    if (child == 0)
    {
        std::vector<std::string> argv_storage = owner_args;
        std::vector<char*> argv;
        argv.reserve(argv_storage.size() + 1);
        for (auto& arg : argv_storage)
            argv.push_back(arg.data());
        argv.push_back(nullptr);
        execv(exe_path.string().c_str(), argv.data());
        _exit(127);
    }
    return true;
#endif
}

enum class SessionOwnerLaunchStatus
{
    Attached,
    SpawnFailed,
    AttachFailed,
};

SessionOwnerLaunchStatus launch_session_owner_and_attach(
    const std::vector<std::string>& args, const draxul::ParsedArgs& parsed, std::string* error)
{
    PERF_MEASURE();
    const auto exe_path = executable_path(args);
    if (exe_path.empty())
    {
        if (error)
            *error = "Failed to resolve the Draxul executable path.";
        return SessionOwnerLaunchStatus::SpawnFailed;
    }

    const auto owner_args = session_owner_args(args, exe_path, parsed);
    if (!spawn_session_owner_process(exe_path, owner_args, error))
        return SessionOwnerLaunchStatus::SpawnFailed;

    constexpr auto kAttachTimeout = std::chrono::seconds(10);
    constexpr auto kAttachPoll = std::chrono::milliseconds(50);
    const auto deadline = std::chrono::steady_clock::now() + kAttachTimeout;
    std::string last_error;
    while (std::chrono::steady_clock::now() < deadline)
    {
        last_error.clear();
        const auto attach_status = draxul::SessionAttachServer::try_attach(parsed.session_id, &last_error);
        if (attach_status == draxul::SessionAttachServer::AttachStatus::Attached)
            return SessionOwnerLaunchStatus::Attached;
        std::this_thread::sleep_for(kAttachPoll);
    }

    if (error)
    {
        *error = last_error.empty()
            ? "Timed out waiting for the session owner to accept an attach request."
            : ("Timed out waiting for the session owner to accept an attach request: " + last_error);
    }
    return SessionOwnerLaunchStatus::AttachFailed;
}

bool try_attach_with_retry(std::string_view session_id, std::chrono::milliseconds timeout, std::string* error)
{
    constexpr auto kPoll = std::chrono::milliseconds(50);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::string last_error;
    int attempt = 0;

    DRAXUL_LOG_DEBUG(draxul::LogCategory::App,
        "Retrying session attach for '%s' for up to %lld ms",
        std::string(session_id).c_str(),
        static_cast<long long>(timeout.count()));

    while (std::chrono::steady_clock::now() < deadline)
    {
        ++attempt;
        last_error.clear();
        const auto attach_status = draxul::SessionAttachServer::try_attach(session_id, &last_error);
        if (attach_status == draxul::SessionAttachServer::AttachStatus::Attached)
        {
            DRAXUL_LOG_DEBUG(draxul::LogCategory::App,
                "Retry attach for '%s' succeeded on attempt %d",
                std::string(session_id).c_str(),
                attempt);
            return true;
        }
        DRAXUL_LOG_DEBUG(draxul::LogCategory::App,
            "Retry attach attempt %d for '%s' returned status=%d error='%s'",
            attempt,
            std::string(session_id).c_str(),
            static_cast<int>(attach_status),
            last_error.c_str());
        std::this_thread::sleep_for(kPoll);
    }

    if (error)
        *error = last_error;
    DRAXUL_LOG_WARN(draxul::LogCategory::App,
        "Retry attach for '%s' timed out after %d attempts; last error='%s'",
        std::string(session_id).c_str(),
        attempt,
        last_error.c_str());
    return false;
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
        || parse_result.args.attach_session
        || parse_result.args.detach_session
        || parse_result.args.rename_session
        || parse_result.args.kill_session;
    if (needs_console_output && !parse_result.args.session_owner)
        ensure_console_io(true);
#endif
    if (parse_result.error)
    {
        std::fprintf(stderr, "%s\n", parse_result.error->c_str());
        return 1;
    }
    draxul::ParsedArgs& parsed = parse_result.args;

    std::string new_session_error;
    if (!prepare_new_session_launch(parsed, &new_session_error))
    {
        // Don't exit — fall back to a default session. The window must appear.
        DRAXUL_LOG_WARN(draxul::LogCategory::App,
            "Session launch preparation failed: %s — using default session",
            new_session_error.empty() ? "unknown error" : new_session_error.c_str());
        parsed.session_id = "default";
        parsed.new_session = false;
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

    if (parsed.list_sessions)
    {
        std::string list_error;
        const auto sessions = draxul::list_known_sessions(&list_error);
        if (!list_error.empty())
        {
            std::fprintf(stderr, "Failed to list sessions: %s\n", list_error.c_str());
            draxul::shutdown_logging();
            return 1;
        }
        if (sessions.empty())
        {
            std::printf("No saved sessions.\n");
            draxul::shutdown_logging();
            return 0;
        }
        const std::string table = draxul::format_session_listing_table(sessions);
        std::fputs(table.c_str(), stdout);
        draxul::shutdown_logging();
        return 0;
    }

    if (parsed.attach_session)
    {
        std::string attach_error;
        const auto attach_status = draxul::SessionAttachServer::try_attach(
            parsed.session_id, &attach_error);
        if (attach_status == draxul::SessionAttachServer::AttachStatus::Attached)
        {
            std::printf("Attached to session '%s'.\n", parsed.session_id.c_str());
            draxul::shutdown_logging();
            return 0;
        }
        if (attach_status == draxul::SessionAttachServer::AttachStatus::NoServer)
        {
            std::fprintf(stderr, "No running session '%s'.\n", parsed.session_id.c_str());
            draxul::shutdown_logging();
            return 1;
        }
        std::fprintf(stderr, "Failed to attach to session '%s': %s\n",
            parsed.session_id.c_str(), attach_error.empty() ? "unknown error" : attach_error.c_str());
        draxul::shutdown_logging();
        return 1;
    }

    if (parsed.detach_session)
    {
        std::string command_error;
        const auto detach_status = draxul::SessionAttachServer::send_command(
            parsed.session_id, draxul::SessionAttachServer::Command::Detach, &command_error);
        if (detach_status == draxul::SessionAttachServer::AttachStatus::Attached)
        {
            constexpr auto kDetachTimeout = std::chrono::seconds(1);
            constexpr auto kDetachPoll = std::chrono::milliseconds(50);
            const auto deadline = std::chrono::steady_clock::now() + kDetachTimeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                draxul::SessionAttachServer::LiveSessionInfo live_info;
                if (draxul::SessionAttachServer::query_live_session(parsed.session_id, &live_info)
                    && live_info.detached)
                {
                    std::printf("Detached session '%s'.\n", parsed.session_id.c_str());
                    draxul::shutdown_logging();
                    return 0;
                }
                std::this_thread::sleep_for(kDetachPoll);
            }
            std::fprintf(stderr,
                "Session '%s' is running but did not detach. It may not be a detachable shell session.\n",
                parsed.session_id.c_str());
            draxul::shutdown_logging();
            return 1;
        }
        if (detach_status == draxul::SessionAttachServer::AttachStatus::NoServer)
        {
            std::fprintf(stderr, "No running session '%s'.\n", parsed.session_id.c_str());
            draxul::shutdown_logging();
            return 1;
        }
        std::fprintf(stderr, "Failed to detach session '%s': %s\n",
            parsed.session_id.c_str(), command_error.empty() ? "unknown error" : command_error.c_str());
        draxul::shutdown_logging();
        return 1;
    }

    if (parsed.rename_session)
    {
        std::string command_error;
        if (draxul::SessionAttachServer::rename_session(
                parsed.session_id, parsed.session_name, &command_error))
        {
            std::printf("Renamed session '%s' to '%s'.\n",
                parsed.session_id.c_str(),
                parsed.session_name.c_str());
            draxul::shutdown_logging();
            return 0;
        }

        std::string rename_error;
        if (rename_saved_session_records(parsed.session_id, parsed.session_name, &rename_error))
        {
            std::printf("Renamed saved session '%s' to '%s'.\n",
                parsed.session_id.c_str(),
                parsed.session_name.c_str());
            draxul::shutdown_logging();
            return 0;
        }

        const char* message = !rename_error.empty() ? rename_error.c_str()
                                                    : (!command_error.empty() ? command_error.c_str() : "unknown error");
        std::fprintf(stderr, "Failed to rename session '%s': %s\n",
            parsed.session_id.c_str(), message);
        draxul::shutdown_logging();
        return 1;
    }

    if (parsed.kill_session)
    {
        std::string command_error;
        const auto kill_status = draxul::SessionAttachServer::send_command(
            parsed.session_id, draxul::SessionAttachServer::Command::Shutdown, &command_error);
        if (kill_status == draxul::SessionAttachServer::AttachStatus::Attached)
        {
            std::printf("Killed running session '%s'.\n", parsed.session_id.c_str());
            (void)draxul::delete_session_state(parsed.session_id);
            (void)draxul::delete_session_runtime_metadata(parsed.session_id);
            draxul::shutdown_logging();
            return 0;
        }

        std::string delete_error;
        const bool deleted_saved_state = draxul::delete_session_state(parsed.session_id, &delete_error);
        const bool deleted_metadata = draxul::delete_session_runtime_metadata(parsed.session_id);
        if (kill_status == draxul::SessionAttachServer::AttachStatus::NoServer
            && (deleted_saved_state || deleted_metadata))
        {
            std::printf("Deleted saved session '%s'.\n", parsed.session_id.c_str());
            draxul::shutdown_logging();
            return 0;
        }

        if (kill_status == draxul::SessionAttachServer::AttachStatus::NoServer)
        {
            std::fprintf(stderr, "No running or saved session '%s'.\n", parsed.session_id.c_str());
            draxul::shutdown_logging();
            return 1;
        }

        std::fprintf(stderr, "Failed to kill session '%s': %s\n",
            parsed.session_id.c_str(), command_error.empty() ? "unknown error" : command_error.c_str());
        draxul::shutdown_logging();
        return 1;
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
    // --session-owner is the legacy spelling for the opt-in persistent app
    // mode. Keep it quiet so old shortcuts still behave like they used to.

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
    options.session_id = parsed.session_id;
    if (!parsed.session_name.empty())
        options.session_name = parsed.session_name;
    options.new_session_requested = parsed.new_session;
    if (parsed.pick_session)
    {
        const auto picker_exe_path = executable_path(args);
        options.enable_session_attach = false;
        options.enable_session_restore = false;
        options.host_factory = [picker_exe_path](draxul::HostKind /*kind*/) {
            return std::make_unique<draxul::SessionPickerHost>(picker_exe_path);
        };
    }

#ifdef DRAXUL_ENABLE_RENDER_TESTS
    const bool allow_session_restore = !parsed.smoke_test
        && !render_test.has_value()
        && parsed.screenshot_path.empty()
        && !parsed.pick_session;
#else
    const bool allow_session_restore = !parsed.smoke_test
        && parsed.screenshot_path.empty()
        && !parsed.pick_session;
#endif
    const bool enable_live_session_attach
        = allow_session_restore && (parsed.persistent_app || parsed.session_owner);
    options.enable_session_restore = allow_session_restore;
    options.enable_session_attach = enable_live_session_attach;

    if (enable_live_session_attach)
    {
        // Persistent-app mode: if another live owner is already running for
        // this session, tell it to show its window and exit.
        DRAXUL_LOG_DEBUG(draxul::LogCategory::App,
            "Startup attach probe for session '%s' (persistent_app=%d, session_owner=%d)",
            parsed.session_id.c_str(),
            parsed.persistent_app ? 1 : 0,
            parsed.session_owner ? 1 : 0);
        std::string attach_error;
        const auto attach_status = draxul::SessionAttachServer::try_attach(
            parsed.session_id, &attach_error);
        if (attach_status == draxul::SessionAttachServer::AttachStatus::Attached)
        {
            DRAXUL_LOG_DEBUG(draxul::LogCategory::App,
                "Startup attach to session '%s' succeeded; exiting helper process",
                parsed.session_id.c_str());
            draxul::shutdown_logging();
            return 0;
        }
        // Any other status (NoServer, Error) — proceed with in-process startup.
        DRAXUL_LOG_DEBUG(draxul::LogCategory::App,
            "Startup attach to session '%s' did not attach (status=%d, error='%s')",
            parsed.session_id.c_str(),
            static_cast<int>(attach_status),
            attach_error.c_str());
    }

    draxul::App app(std::move(options));

    if (!app.initialize())
    {
        if (enable_live_session_attach
            && app.init_error().find("session-attach") != std::string::npos)
        {
            DRAXUL_LOG_WARN(draxul::LogCategory::App,
                "App initialization hit session-attach error for '%s': %s",
                parsed.session_id.c_str(),
                app.init_error().c_str());
            std::string attach_error;
            if (try_attach_with_retry(parsed.session_id, std::chrono::seconds(2), &attach_error))
            {
                DRAXUL_LOG_DEBUG(draxul::LogCategory::App,
                    "Recovered from session-attach initialization error by attaching to '%s'",
                    parsed.session_id.c_str());
                draxul::shutdown_logging();
                return 0;
            }
            if (!attach_error.empty())
            {
                DRAXUL_LOG_WARN(draxul::LogCategory::App,
                    "Retry attach after session-server collision failed: %s", attach_error.c_str());
            }
        }

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
