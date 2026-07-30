#include "server_status_surface.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_tray.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <draxul/server_client.h>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace draxul
{

namespace
{

std::string count_label(
    size_t count, std::string_view singular,
    std::string_view plural)
{
    return std::to_string(count) + " "
        + std::string(count == 1 ? singular : plural);
}

#ifdef _WIN32
std::wstring quote_windows_argument(
    const std::wstring& argument)
{
    if (argument.find_first_of(L" \t\"")
        == std::wstring::npos)
    {
        return argument;
    }
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (const wchar_t ch : argument)
    {
        if (ch == L'\\')
        {
            ++backslashes;
            continue;
        }
        if (ch == L'"')
        {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}
#endif

bool confirm_action(
    const char* title, const std::string& message,
    const char* action_label)
{
    const std::array<SDL_MessageBoxButtonData, 2> buttons{ {
        {
            SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
            0,
            "Cancel",
        },
        {
            SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,
            1,
            action_label,
        },
    } };
    const SDL_MessageBoxData data{
        .flags = SDL_MESSAGEBOX_WARNING
            | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT,
        .window = nullptr,
        .title = title,
        .message = message.c_str(),
        .numbuttons = static_cast<int>(buttons.size()),
        .buttons = buttons.data(),
        .colorScheme = nullptr,
    };
    int selected = 0;
    return SDL_ShowMessageBox(&data, &selected)
        && selected == 1;
}

void show_error(
    const char* title, const std::string& message)
{
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR, title, message.c_str(), nullptr);
}

SDL_Surface* make_status_icon()
{
    SDL_Surface* icon = SDL_CreateSurface(
        32, 32, SDL_PIXELFORMAT_RGBA32);
    if (!icon)
        return nullptr;
    const Uint32 transparent
        = SDL_MapSurfaceRGBA(icon, 0, 0, 0, 0);
    const Uint32 red
        = SDL_MapSurfaceRGBA(icon, 224, 96, 112, 255);
    const Uint32 dark
        = SDL_MapSurfaceRGBA(icon, 30, 30, 46, 255);
    SDL_FillSurfaceRect(icon, nullptr, transparent);
    const std::array<SDL_Rect, 4> red_rects{ {
        { 5, 3, 7, 26 },
        { 11, 3, 9, 5 },
        { 11, 24, 9, 5 },
        { 20, 8, 7, 16 },
    } };
    SDL_FillSurfaceRects(
        icon, red_rects.data(),
        static_cast<int>(red_rects.size()), red);
    const SDL_Rect center{ 12, 9, 8, 14 };
    SDL_FillSurfaceRect(icon, &center, dark);
    return icon;
}

} // namespace

ServerStatusText format_server_status_text(
    const ServerStatusSnapshot& status)
{
    size_t live_terminals = 0;
    for (const auto& session : status.session_statuses)
        live_terminals += session.live_terminals;
    return {
        .state = "Draxul Server - "
            + (status.state.empty()
                    ? std::string("unknown")
                    : status.state),
        .clients_and_terminals
        = count_label(
              status.connected_clients, "client", "clients")
            + " - "
            + count_label(
                live_terminals,
                "live terminal", "live terminals")
            + " / "
            + count_label(
                status.terminals, "terminal", "terminals"),
        .sessions_spaces_and_agents
        = count_label(status.sessions, "Session", "Sessions")
            + " - "
            + count_label(status.spaces, "Space", "Spaces")
            + " - "
            + count_label(status.agents, "Agent", "Agents"),
    };
}

std::string format_server_status_summary(
    const ServerStatusSnapshot& status)
{
    const auto text = format_server_status_text(status);
    return text.state + "; " + text.clients_and_terminals
        + "; " + text.sessions_spaces_and_agents;
}

std::string format_server_session_listing_table(
    const std::vector<ServerSessionStatusSnapshot>& sessions)
{
    size_t id_width = std::string_view("SESSION ID").size();
    size_t name_width = std::string_view("NAME").size();
    const size_t space_width
        = std::string_view("SPACES").size();
    const size_t terminal_width
        = std::string_view("TERMINALS").size();
    const size_t live_width = std::string_view("LIVE").size();
    const size_t checkpoint_width
        = std::string_view("CHECKPOINT").size();

    for (const auto& session : sessions)
    {
        id_width = std::max(id_width, session.session_id.size());
        name_width = std::max(name_width,
            (session.session_name.empty()
                    ? session.session_id
                    : session.session_name)
                .size());
    }

    std::ostringstream out;
    out << std::left
        << std::setw(static_cast<int>(id_width))
        << "SESSION ID"
        << "  " << std::left
        << std::setw(static_cast<int>(name_width))
        << "NAME"
        << "  " << std::left
        << std::setw(static_cast<int>(space_width))
        << "SPACES"
        << "  " << std::left
        << std::setw(static_cast<int>(terminal_width))
        << "TERMINALS"
        << "  " << std::left
        << std::setw(static_cast<int>(live_width))
        << "LIVE"
        << "  CHECKPOINT\n";

    out << std::string(id_width, '-')
        << "  " << std::string(name_width, '-')
        << "  " << std::string(space_width, '-')
        << "  " << std::string(terminal_width, '-')
        << "  " << std::string(live_width, '-')
        << "  " << std::string(checkpoint_width, '-')
        << '\n';

    for (const auto& session : sessions)
    {
        out << std::left
            << std::setw(static_cast<int>(id_width))
            << session.session_id
            << "  " << std::left
            << std::setw(static_cast<int>(name_width))
            << (session.session_name.empty()
                    ? session.session_id
                    : session.session_name)
            << "  " << std::right
            << std::setw(static_cast<int>(space_width))
            << session.spaces
            << "  " << std::right
            << std::setw(static_cast<int>(terminal_width))
            << session.terminals
            << "  " << std::right
            << std::setw(static_cast<int>(live_width))
            << session.live_terminals
            << "  " << session.checkpoint_state << '\n';
    }
    return out.str();
}

std::filesystem::path default_server_log_path(
    const std::filesystem::path& runtime_directory)
{
    return runtime_directory / "draxul-server.log";
}

bool launch_draxul_ui(const std::filesystem::path& executable,
    const std::filesystem::path& runtime_directory,
    std::string& error)
{
    if (executable.empty()
        || !std::filesystem::exists(executable))
    {
        error = "The Draxul executable is unavailable.";
        return false;
    }
#ifdef _WIN32
    std::wstring command
        = quote_windows_argument(executable.wstring())
        + L" --server-runtime-dir "
        + quote_windows_argument(runtime_directory.wstring());
    std::vector<wchar_t> mutable_command(
        command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.wstring().c_str(),
            mutable_command.data(), nullptr, nullptr, FALSE,
            CREATE_NEW_PROCESS_GROUP, nullptr, nullptr,
            &startup, &process))
    {
        error = "Unable to open Draxul (error "
            + std::to_string(GetLastError()) + ").";
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    const pid_t child = ::fork();
    if (child < 0)
    {
        error = "Unable to fork a Draxul UI process.";
        return false;
    }
    if (child == 0)
    {
        ::setsid();
        const std::string executable_text = executable.string();
        const std::string runtime_text
            = runtime_directory.string();
        ::execl(executable_text.c_str(),
            executable_text.c_str(),
            "--server-runtime-dir",
            runtime_text.c_str(), nullptr);
        _exit(127);
    }
    return true;
#endif
}

bool open_server_log(
    const std::filesystem::path& log_path, std::string& error)
{
    if (log_path.empty()
        || !std::filesystem::exists(log_path))
    {
        error = "The Draxul server log does not exist yet.";
        return false;
    }
#ifdef _WIN32
    const auto result = reinterpret_cast<intptr_t>(
        ShellExecuteW(nullptr, L"open",
            log_path.wstring().c_str(), nullptr, nullptr,
            SW_SHOWNORMAL));
    if (result <= 32)
    {
        error = "Windows could not open the Draxul server log.";
        return false;
    }
    return true;
#else
    const pid_t child = ::fork();
    if (child < 0)
    {
        error = "Unable to launch the system file viewer.";
        return false;
    }
    if (child == 0)
    {
        ::setsid();
        const std::string path = log_path.string();
#ifdef __APPLE__
        ::execlp("open", "open", path.c_str(), nullptr);
#else
        ::execlp("xdg-open", "xdg-open", path.c_str(), nullptr);
#endif
        _exit(127);
    }
    return true;
#endif
}

class ServerStatusSurface::Impl
{
public:
    explicit Impl(ServerStatusSurfaceOptions value)
        : options(std::move(value))
    {
        if (options.log_path.empty())
        {
            options.log_path = default_server_log_path(
                options.runtime_directory);
        }
    }

    bool initialize(std::string& error)
    {
#ifdef __APPLE__
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
#endif
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            error = SDL_GetError();
            return false;
        }
        video_initialized = true;
        SDL_Surface* icon = make_status_icon();
        tray = SDL_CreateTray(icon, "Draxul Server");
        SDL_DestroySurface(icon);
        if (!tray)
        {
            error = SDL_GetError();
            shutdown();
            return false;
        }
        SDL_TrayMenu* menu = SDL_CreateTrayMenu(tray);
        if (!menu)
        {
            error = SDL_GetError();
            shutdown();
            return false;
        }
        state_entry = SDL_InsertTrayEntryAt(
            menu, -1, "Draxul Server - starting",
            SDL_TRAYENTRY_BUTTON | SDL_TRAYENTRY_DISABLED);
        clients_entry = SDL_InsertTrayEntryAt(
            menu, -1, "0 clients - 0 live terminals",
            SDL_TRAYENTRY_BUTTON | SDL_TRAYENTRY_DISABLED);
        topology_entry = SDL_InsertTrayEntryAt(
            menu, -1, "0 Sessions - 0 Spaces - 0 Agents",
            SDL_TRAYENTRY_BUTTON | SDL_TRAYENTRY_DISABLED);
        SDL_InsertTrayEntryAt(menu, -1, nullptr, 0);
        add_action(menu, "Open Draxul", &Impl::open_callback);
        add_action(menu, "Refresh Status", &Impl::refresh_callback);
        add_action(menu, "Open Server Log", &Impl::log_callback);
        SDL_InsertTrayEntryAt(menu, -1, nullptr, 0);
        add_action(menu, "Stop Server...", &Impl::stop_callback);
        add_action(menu, "Force Stop Server...",
            &Impl::force_stop_callback);
        if (!state_entry || !clients_entry
            || !topology_entry)
        {
            error = SDL_GetError();
            shutdown();
            return false;
        }
        refresh();
        return true;
    }

    void pump()
    {
        if (!tray)
            return;
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            // Tray callbacks are dispatched by SDL_PollEvent. A server does
            // not own an application window, so SDL quit events are not
            // interpreted as permission to destroy live terminals.
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_refresh)
            refresh();
        SDL_Delay(20);
    }

    void shutdown()
    {
        if (tray)
        {
            SDL_DestroyTray(tray);
            tray = nullptr;
        }
        state_entry = nullptr;
        clients_entry = nullptr;
        topology_entry = nullptr;
        if (video_initialized)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            video_initialized = false;
        }
    }

    void refresh()
    {
        next_refresh = std::chrono::steady_clock::now()
            + std::chrono::seconds(1);
        const auto result
            = ServerClient::status(options.runtime_directory);
        if (!result.ok || !result.status)
        {
            if (state_entry)
            {
                SDL_SetTrayEntryLabel(
                    state_entry, "Draxul Server - status unavailable");
            }
            return;
        }
        const auto text = format_server_status_text(*result.status);
        SDL_SetTrayEntryLabel(state_entry, text.state.c_str());
        SDL_SetTrayEntryLabel(
            clients_entry, text.clients_and_terminals.c_str());
        SDL_SetTrayEntryLabel(topology_entry,
            text.sessions_spaces_and_agents.c_str());
        const std::string tooltip
            = format_server_status_summary(*result.status);
        SDL_SetTrayTooltip(tray, tooltip.c_str());
    }

    void open()
    {
        std::string error;
        if (!launch_draxul_ui(options.executable_path,
                options.runtime_directory, error))
        {
            show_error("Draxul Server", error);
        }
    }

    void open_log()
    {
        std::string error;
        if (!open_server_log(options.log_path, error))
            show_error("Draxul Server Log", error);
    }

    void stop()
    {
        const auto status
            = ServerClient::status(options.runtime_directory);
        if (!status.ok || !status.status)
        {
            show_error("Stop Draxul Server",
                status.error_message.empty()
                    ? "The Draxul server status is unavailable."
                    : status.error_message);
            return;
        }
        size_t live_terminals = 0;
        for (const auto& session
            : status.status->session_statuses)
        {
            live_terminals += session.live_terminals;
        }
        std::string message
            = "Stop the Draxul server?";
        if (live_terminals > 0)
        {
            message = "This will close "
                + count_label(live_terminals,
                    "live terminal", "live terminals")
                + " and their processes. Continue?";
        }
        if (!confirm_action(
                "Stop Draxul Server", message, "Stop Server"))
        {
            return;
        }
        std::string error;
        if (!ServerClient::shutdown(
                options.runtime_directory,
                { .confirm_live_terminals = true }, error))
        {
            show_error("Stop Draxul Server", error);
        }
    }

    void force_stop()
    {
        if (!confirm_action("Force Stop Draxul Server",
                "Force stop bypasses the final checkpoint and immediately "
                "destroys all terminal processes. Use it only when graceful "
                "stop is not working.",
                "Force Stop"))
        {
            return;
        }
        std::string error;
        if (!ServerClient::force_stop(
                options.runtime_directory, true, error))
        {
            show_error("Force Stop Draxul Server", error);
        }
    }

    void add_action(SDL_TrayMenu* menu, const char* label,
        SDL_TrayCallback callback)
    {
        SDL_TrayEntry* entry = SDL_InsertTrayEntryAt(
            menu, -1, label, SDL_TRAYENTRY_BUTTON);
        if (entry)
            SDL_SetTrayEntryCallback(entry, callback, this);
    }

    static void SDLCALL open_callback(
        void* userdata, SDL_TrayEntry*)
    {
        static_cast<Impl*>(userdata)->open();
    }

    static void SDLCALL refresh_callback(
        void* userdata, SDL_TrayEntry*)
    {
        static_cast<Impl*>(userdata)->refresh();
    }

    static void SDLCALL log_callback(
        void* userdata, SDL_TrayEntry*)
    {
        static_cast<Impl*>(userdata)->open_log();
    }

    static void SDLCALL stop_callback(
        void* userdata, SDL_TrayEntry*)
    {
        static_cast<Impl*>(userdata)->stop();
    }

    static void SDLCALL force_stop_callback(
        void* userdata, SDL_TrayEntry*)
    {
        static_cast<Impl*>(userdata)->force_stop();
    }

    ServerStatusSurfaceOptions options;
    SDL_Tray* tray = nullptr;
    SDL_TrayEntry* state_entry = nullptr;
    SDL_TrayEntry* clients_entry = nullptr;
    SDL_TrayEntry* topology_entry = nullptr;
    bool video_initialized = false;
    std::chrono::steady_clock::time_point next_refresh{};
};

ServerStatusSurface::ServerStatusSurface(
    ServerStatusSurfaceOptions options)
    : impl_(std::make_unique<Impl>(std::move(options)))
{
}

ServerStatusSurface::~ServerStatusSurface()
{
    shutdown();
}

bool ServerStatusSurface::initialize(std::string& error)
{
    return impl_->initialize(error);
}

void ServerStatusSurface::pump()
{
    impl_->pump();
}

void ServerStatusSurface::shutdown()
{
    if (impl_)
        impl_->shutdown();
}

bool ServerStatusSurface::available() const
{
    return impl_ && impl_->tray != nullptr;
}

} // namespace draxul
