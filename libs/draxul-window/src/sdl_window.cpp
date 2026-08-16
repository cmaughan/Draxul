#include "sdl_clipboard.h"
#include "sdl_event_translator.h"
#include "sdl_file_dialog.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <draxul/sdl_window.h>
#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#include <SDL3/SDL_metal.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <SDL3/SDL_vulkan.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <windows.h>
#else
#include <SDL3/SDL_vulkan.h>
#endif
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/string_util.h>

namespace draxul
{

#ifdef __APPLE__
extern void apply_title_bar_color_macos(SDL_Window*, Color);
extern void disable_press_and_hold_macos();
#endif

namespace
{

const char* safe_key_name(int keycode)
{
    const char* name = SDL_GetKeyName(static_cast<SDL_Keycode>(keycode));
    return (name && *name) ? name : "<unknown>";
}

} // namespace

SdlWindow::~SdlWindow()
{
    shutdown();
}

#if defined(_WIN32) || defined(__APPLE__)
static void log_display_info(SDL_Window* window)
{
    PERF_MEASURE();
    DRAXUL_LOG_DEBUG(LogCategory::Window, "Display / DPI diagnostics begin");

    // SDL window sizes
    int lw = 0, lh = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(window, &lw, &lh);
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    float sdl_scale = SDL_GetWindowDisplayScale(window);
    DRAXUL_LOG_DEBUG(LogCategory::Window, "SDL logical size: %d x %d", lw, lh);
    DRAXUL_LOG_DEBUG(LogCategory::Window, "SDL pixel size: %d x %d", pw, ph);
    DRAXUL_LOG_DEBUG(LogCategory::Window, "SDL display scale: %.3f", sdl_scale);
    DRAXUL_LOG_DEBUG(LogCategory::Window, "SDL effective DPI: %.1f", 96.0f * sdl_scale);

#ifdef __APPLE__
    // CoreGraphics display info
    CGDirectDisplayID displayID = CGMainDisplayID();
    CGSize physicalSize = CGDisplayScreenSize(displayID); // millimeters
    size_t cgLogicalW = CGDisplayPixelsWide(displayID);
    size_t cgLogicalH = CGDisplayPixelsHigh(displayID);
    size_t cgPhysicalW = cgLogicalW, cgPhysicalH = cgLogicalH;
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayID);
    if (mode)
    {
        cgPhysicalW = CGDisplayModeGetPixelWidth(mode);
        cgPhysicalH = CGDisplayModeGetPixelHeight(mode);
        CGDisplayModeRelease(mode);
    }
    DRAXUL_LOG_DEBUG(LogCategory::Window, "CG logical pixels: %zu x %zu", cgLogicalW, cgLogicalH);
    DRAXUL_LOG_DEBUG(LogCategory::Window, "CG physical pixels: %zu x %zu", cgPhysicalW, cgPhysicalH);
    DRAXUL_LOG_DEBUG(LogCategory::Window, "CG physical size: %.1f x %.1f mm", physicalSize.width, physicalSize.height);
    if (physicalSize.width > 0)
    {
        auto ppi_logical = static_cast<double>(cgLogicalW) / (physicalSize.width / 25.4);
        auto ppi_physical = static_cast<double>(cgPhysicalW) / (physicalSize.width / 25.4);
        DRAXUL_LOG_DEBUG(LogCategory::Window, "Computed PPI (logical): %.1f", ppi_logical);
        DRAXUL_LOG_DEBUG(LogCategory::Window, "Computed PPI (physical): %.1f", ppi_physical);
    }
    SDL_DisplayID sdlDisplay = SDL_GetDisplayForWindow(window);
    float contentScale = sdlDisplay ? SDL_GetDisplayContentScale(sdlDisplay) : 1.0f;
    DRAXUL_LOG_DEBUG(LogCategory::Window, "SDL content scale: %.3f", contentScale);
#endif

#ifdef _WIN32
    // Win32 HWND-based queries
    HWND hwnd = (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (hwnd)
    {
        // Per-monitor DPI variants
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        UINT eff_x = 0, eff_y = 0, ang_x = 0, ang_y = 0, raw_x = 0, raw_y = 0;
        GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &eff_x, &eff_y);
        GetDpiForMonitor(monitor, MDT_ANGULAR_DPI, &ang_x, &ang_y);
        GetDpiForMonitor(monitor, MDT_RAW_DPI, &raw_x, &raw_y);
        DRAXUL_LOG_DEBUG(LogCategory::Window, "MDT_EFFECTIVE_DPI: %u x %u", eff_x, eff_y);
        DRAXUL_LOG_DEBUG(LogCategory::Window, "MDT_ANGULAR_DPI: %u x %u", ang_x, ang_y);
        DRAXUL_LOG_DEBUG(LogCategory::Window, "MDT_RAW_DPI: %u x %u", raw_x, raw_y);

        // Monitor logical/physical rect
        MONITORINFOEX mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfo(monitor, &mi);
        int mon_lw = mi.rcMonitor.right - mi.rcMonitor.left;
        int mon_lh = mi.rcMonitor.bottom - mi.rcMonitor.top;
        DRAXUL_LOG_DEBUG(LogCategory::Window, "Monitor logical rect: %d x %d (%s)",
            mon_lw, mon_lh,
            (mi.dwFlags & MONITORINFOF_PRIMARY) ? "primary" : "non-primary");

        // Physical pixel resolution via DEVMODE
        DEVMODE dm = {};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettings(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        {
            DRAXUL_LOG_DEBUG(LogCategory::Window, "Monitor device pixels: %lu x %lu",
                dm.dmPelsWidth, dm.dmPelsHeight);
            HDC hdc = CreateDC(NULL, mi.szDevice, NULL, NULL);
            if (hdc)
            {
                int mm_w = GetDeviceCaps(hdc, HORZSIZE);
                int mm_h = GetDeviceCaps(hdc, VERTSIZE);
                int log_px_w = GetDeviceCaps(hdc, HORZRES);
                int log_px_h = GetDeviceCaps(hdc, VERTRES);
                int desk_px_w = GetDeviceCaps(hdc, DESKTOPHORZRES);
                int desk_px_h = GetDeviceCaps(hdc, DESKTOPVERTRES);
                int logpx_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
                DeleteDC(hdc);
                DRAXUL_LOG_DEBUG(LogCategory::Window, "GetDeviceCaps HORZSIZE/VERTSIZE: %d x %d mm", mm_w, mm_h);
                DRAXUL_LOG_DEBUG(LogCategory::Window, "GetDeviceCaps HORZRES/VERTRES: %d x %d", log_px_w, log_px_h);
                DRAXUL_LOG_DEBUG(LogCategory::Window,
                    "GetDeviceCaps DESKTOPHORZRES/VERTRES: %d x %d", desk_px_w, desk_px_h);
                DRAXUL_LOG_DEBUG(LogCategory::Window, "GetDeviceCaps LOGPIXELSX: %d", logpx_dpi);
                if (mm_w > 0)
                {
                    float phys_ppi = dm.dmPelsWidth / (mm_w / 25.4f);
                    DRAXUL_LOG_DEBUG(LogCategory::Window, "Computed physical PPI (DEVMODE/mm): %.1f", phys_ppi);
                }
            }
        }
    }
#endif
    DRAXUL_LOG_DEBUG(LogCategory::Window, "Display / DPI diagnostics end");
}
#endif // _WIN32 || __APPLE__

bool SdlWindow::initialize(const std::string& title, int width, int height)
{
    PERF_MEASURE();
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "1");
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        DRAXUL_LOG_ERROR(LogCategory::Window, "SDL_Init failed: %s", SDL_GetError());
        return false;
    }

#ifdef __APPLE__
    // SDL3's Cocoa backend registers ApplePressAndHoldEnabled=YES via
    // registerDefaults:, which replaces key repeat with macOS accent pickers.
    // Override it to NO so that holding a key generates OS repeat events.
    disable_press_and_hold_macos();
#endif

    wake_event_type_ = SDL_RegisterEvents(2);
    if (wake_event_type_ == static_cast<Uint32>(-1))
    {
        DRAXUL_LOG_ERROR(LogCategory::Window, "SDL_RegisterEvents failed: %s", SDL_GetError());
        return false;
    }
    file_dialog_event_type_ = wake_event_type_ + 1;

#ifdef __APPLE__
    Uint64 window_flags = SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#else
    Uint64 window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif

    if (hidden_)
        window_flags |= SDL_WINDOW_HIDDEN;

    if (clamp_to_display_)
    {
        SDL_Rect usable_bounds = {};
        SDL_DisplayID display = SDL_GetPrimaryDisplay();
        if (display && SDL_GetDisplayUsableBounds(display, &usable_bounds))
        {
            int max_width = usable_bounds.w - 80;
            int max_height = usable_bounds.h - 80;
            if (max_width < 640)
                max_width = 640;
            if (max_height < 400)
                max_height = 400;
            if (width > max_width)
                width = max_width;
            if (height > max_height)
                height = max_height;
        }
    }

#ifdef _WIN32
    const std::string window_title = title;
#else
    const std::string window_title = "\U0001FA78 " + title;
#endif
    window_ = SDL_CreateWindow(
        window_title.c_str(),
        width, height,
        window_flags);

    if (!window_)
    {
        DRAXUL_LOG_ERROR(LogCategory::Window, "SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    // Enable text input events (required in SDL3)
    SDL_StartTextInput(window_);

#if defined(_WIN32) || defined(__APPLE__)
    if (log_would_emit(LogLevel::Debug, LogCategory::Window))
        log_display_info(window_);
#endif

    return true;
}

void SdlWindow::set_size_logical(int width, int height)
{
    PERF_MEASURE();
    if (!window_)
        return;

    SDL_SetWindowSize(window_, width, height);
    SDL_SyncWindow(window_);
}

void SdlWindow::activate()
{
    PERF_MEASURE();
    if (!window_)
        return;

    SDL_ShowWindow(window_);
    SDL_RaiseWindow(window_);
    SDL_SyncWindow(window_);

#ifdef _WIN32
    // Win32 activation hack: SDL_RaiseWindow is not always sufficient when the
    // current foreground window belongs to a different thread.  We attach thread
    // input briefly to force the window to the foreground.
    HWND hwnd = (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (hwnd)
    {
        HWND foreground = GetForegroundWindow();
        DWORD current_thread = GetCurrentThreadId();
        DWORD foreground_thread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;

        ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
        if (foreground_thread && foreground_thread != current_thread)
            AttachThreadInput(foreground_thread, current_thread, TRUE);

        // Briefly toggle topmost to keep transient child-console windows from
        // surfacing in front of Draxul during startup host spawn.
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        if (foreground_thread && foreground_thread != current_thread)
            AttachThreadInput(foreground_thread, current_thread, FALSE);
    }
#endif
}

void SdlWindow::shutdown()
{
    PERF_MEASURE();
    clear_callbacks();
    if (cursor_default_)
    {
        SDL_DestroyCursor(cursor_default_);
        cursor_default_ = nullptr;
    }
    if (cursor_ew_)
    {
        SDL_DestroyCursor(cursor_ew_);
        cursor_ew_ = nullptr;
    }
    if (cursor_ns_)
    {
        SDL_DestroyCursor(cursor_ns_);
        cursor_ns_ = nullptr;
    }
    if (cursor_pointer_)
    {
        SDL_DestroyCursor(cursor_pointer_);
        cursor_pointer_ = nullptr;
    }
    if (window_)
    {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

SDL_Cursor* SdlWindow::ensure_cursor(MouseCursor cursor)
{
    switch (cursor)
    {
    case MouseCursor::ResizeLeftRight:
        if (!cursor_ew_)
            cursor_ew_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
        return cursor_ew_;
    case MouseCursor::ResizeUpDown:
        if (!cursor_ns_)
            cursor_ns_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
        return cursor_ns_;
    case MouseCursor::Pointer:
        if (!cursor_pointer_)
            cursor_pointer_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
        return cursor_pointer_;
    case MouseCursor::Default:
    default:
        if (!cursor_default_)
            cursor_default_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        return cursor_default_;
    }
}

void SdlWindow::set_mouse_cursor(MouseCursor cursor)
{
    if (cursor == active_cursor_)
        return;
    if (SDL_Cursor* sc = ensure_cursor(cursor))
    {
        SDL_SetCursor(sc);
        active_cursor_ = cursor;
    }
}

bool SdlWindow::handle_event(const SDL_Event& event)
{
    PERF_MEASURE();
    if (event.type == wake_event_type_)
        return true;

    if (sdl::handle_file_dialog_event(event, file_dialog_event_type_, on_drop_file))
        return true;

    switch (event.type)
    {
    case SDL_EVENT_QUIT:
        // Cmd+Q / menu Quit requests application shutdown.
        if (on_quit_requested)
            on_quit_requested();
        else if (on_close_requested)
            on_close_requested();
        return true;

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        // Window close button requests application shutdown.
        if (on_close_requested)
            on_close_requested();
        break;

    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (on_resize)
            if (auto e = sdl::translate_resize(window_, event))
                on_resize(*e);
        break;

    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        if (on_display_scale_changed)
            if (auto e = sdl::translate_display_scale(window_, event))
                on_display_scale_changed(*e);
        break;

    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (on_key)
            if (auto e = sdl::translate_key(event))
            {
                if (log_would_emit(LogLevel::Trace, LogCategory::Input))
                {
                    log_printf(LogLevel::Trace, LogCategory::Input,
                        "input trace: sdl key type=%s key=%s(%d) scancode=%d mod=0x%X pressed=%d",
                        event.type == SDL_EVENT_KEY_DOWN ? "down" : "up",
                        safe_key_name(e->keycode),
                        e->keycode,
                        e->scancode,
                        static_cast<unsigned int>(e->mod),
                        e->pressed ? 1 : 0);
                }
                on_key(*e);
            }
        break;

    case SDL_EVENT_TEXT_INPUT:
        if (on_text_input)
            if (auto e = sdl::translate_text_input(event))
            {
                if (log_would_emit(LogLevel::Trace, LogCategory::Input))
                {
                    const std::string described = describe_text_for_log(e->text);
                    log_printf(LogLevel::Trace, LogCategory::Input,
                        "input trace: sdl text_input text=%s len=%zu",
                        described.c_str(),
                        e->text.size());
                }
                on_text_input(*e);
            }
        break;

    case SDL_EVENT_TEXT_EDITING:
        if (on_text_editing)
            if (auto e = sdl::translate_text_editing(event))
                on_text_editing(*e);
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (on_mouse_button)
            if (auto e = sdl::translate_mouse_button(event))
            {
                if (log_would_emit(LogLevel::Trace, LogCategory::Input))
                {
                    log_printf(LogLevel::Trace, LogCategory::Input,
                        "input trace: sdl mouse_button button=%d pressed=%d clicks=%d mod=0x%X pos=(%d,%d)",
                        e->button,
                        e->pressed ? 1 : 0,
                        e->clicks,
                        static_cast<unsigned int>(e->mod),
                        e->pos.x,
                        e->pos.y);
                }
                on_mouse_button(*e);
            }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        if (on_mouse_move)
            if (auto e = sdl::translate_mouse_move(event))
                on_mouse_move(*e);
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        if (on_mouse_wheel)
            if (auto e = sdl::translate_mouse_wheel(event))
                on_mouse_wheel(*e);
        break;

    case SDL_EVENT_DROP_FILE:
        if (on_drop_file)
            if (auto e = sdl::translate_file_drop(event))
                on_drop_file(*e);
        break;

    default:
        break;
    }

    return true;
}

bool SdlWindow::poll_events()
{
    PERF_MEASURE();
    flush_text_input_area();
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (!handle_event(event))
            return false;
    }
    return true;
}

bool SdlWindow::wait_events(int timeout_ms)
{
    PERF_MEASURE();
    flush_text_input_area();
    SDL_Event event;
    SDL_ClearError();
    bool got_event = timeout_ms < 0 ? SDL_WaitEvent(&event) : SDL_WaitEventTimeout(&event, timeout_ms);
    if (!got_event)
    {
        const char* err = SDL_GetError();
        if (err && err[0] != '\0')
        {
            DRAXUL_LOG_WARN(LogCategory::Window, "SDL wait failed: %s", err);
        }
        return true;
    }

    if (!handle_event(event))
        return false;

    return poll_events();
}

void SdlWindow::wake()
{
    PERF_MEASURE();
    if (!window_ || wake_event_type_ == 0 || wake_event_type_ == static_cast<Uint32>(-1))
        return;

    SDL_Event event = {};
    event.type = wake_event_type_;
    SDL_PushEvent(&event);
}

std::pair<int, int> SdlWindow::size_pixels() const
{
    PERF_MEASURE();
    int w = 0, h = 0;
    if (window_)
    {
        SDL_GetWindowSizeInPixels(window_, &w, &h);
    }
    return { w, h };
}

bool SdlWindow::is_minimized() const
{
    return window_ != nullptr
        && (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0;
}

std::pair<int, int> SdlWindow::size_logical() const
{
    PERF_MEASURE();
    int w = 0, h = 0;
    if (window_)
    {
        SDL_GetWindowSize(window_, &w, &h);
    }
    return { w, h };
}

float SdlWindow::display_ppi() const
{
    PERF_MEASURE();
    float scale = 1.0f;
    if (window_)
        scale = SDL_GetWindowDisplayScale(window_);
    if (scale <= 0.0f)
        scale = 1.0f;
    return 96.0f * scale;
}

void SdlWindow::set_title(const std::string& title)
{
    PERF_MEASURE();
    if (window_)
#ifdef _WIN32
        SDL_SetWindowTitle(window_, title.c_str());
#else
        SDL_SetWindowTitle(window_, ("\U0001FA78 " + title).c_str());
#endif
}

std::string SdlWindow::clipboard_text() const
{
    return sdl::get_clipboard_text();
}

bool SdlWindow::set_clipboard_text(const std::string& text)
{
    return sdl::set_clipboard_text(text);
}

bool SdlWindow::open_url(std::string_view url)
{
    std::string owned(url);
    return SDL_OpenURL(owned.c_str());
}

void SdlWindow::set_text_input_area(int x, int y, int w, int h)
{
    // Defer the actual SDL call to avoid deadlocking inside Windows IME
    // infrastructure. SDL_SetTextInputArea dispatches a synchronous window
    // message via IME_SetTextInputArea → WIN_WindowProc which can block
    // when called during terminal output processing. We buffer the area
    // and flush it once per frame at the start of poll_events/wait_events.
    text_input_x_ = x;
    text_input_y_ = y;
    text_input_w_ = w;
    text_input_h_ = h;
    text_input_area_dirty_ = true;
}

void SdlWindow::flush_text_input_area()
{
    if (!text_input_area_dirty_ || !window_)
        return;
    text_input_area_dirty_ = false;
    SDL_Rect area = { text_input_x_, text_input_y_, text_input_w_, text_input_h_ };
    SDL_SetTextInputArea(window_, &area, 0);
}

void SdlWindow::set_title_bar_color(Color color)
{
    PERF_MEASURE();
    if (!window_)
        return;

#ifdef __APPLE__
    // Implemented in sdl_window_macos.mm (requires Cocoa/ObjC).
    apply_title_bar_color_macos(window_, color);
#elif defined(_WIN32)
    // DWMWA_CAPTION_COLOR (35) is Windows 11+ and silently ignored on older versions.
    HWND hwnd = (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!hwnd)
        return;
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
    COLORREF cr = RGB(static_cast<BYTE>(color.r * 255.0f), static_cast<BYTE>(color.g * 255.0f),
        static_cast<BYTE>(color.b * 255.0f));
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &cr, sizeof(cr));
#endif
}

void SdlWindow::show_open_file_dialog()
{
    PERF_MEASURE();
    if (!window_ || file_dialog_event_type_ == 0)
        return;
    sdl::show_open_file_dialog(window_, file_dialog_event_type_);
}

void SdlWindow::normalize_render_target_window_size(int target_pixel_width, int target_pixel_height)
{
    PERF_MEASURE();
    if (target_pixel_width <= 0 || target_pixel_height <= 0)
        return;

    auto [logical_w, logical_h] = size_logical();
    auto [pixel_w, pixel_h] = size_pixels();
    if (logical_w <= 0 || logical_h <= 0 || pixel_w <= 0 || pixel_h <= 0)
        return;
    if (pixel_w == target_pixel_width && pixel_h == target_pixel_height)
        return;

    const int target_logical_w = std::max(1,
        static_cast<int>(std::lround(static_cast<double>(logical_w) * target_pixel_width / pixel_w)));
    const int target_logical_h = std::max(1,
        static_cast<int>(std::lround(static_cast<double>(logical_h) * target_pixel_height / pixel_h)));
    set_size_logical(target_logical_w, target_logical_h);
}

} // namespace draxul
