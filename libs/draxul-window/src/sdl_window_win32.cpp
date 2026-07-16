#ifdef _WIN32

#include <draxul/log.h>

#include "platform_callback_test_seams.h"

// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
// clang-format on

#include <functional>
#include <memory>
#include <string>

namespace draxul
{

// ---------------------------------------------------------------------------
// System tray icon for Windows (headless / detached mode)
// ---------------------------------------------------------------------------
// When all windows are closed but sessions are alive, a tray icon provides
// "Attach", "New Session", and "Quit" actions via a right-click context menu.

namespace
{

constexpr UINT WM_TRAY_ICON = WM_USER + 1;
constexpr UINT IDM_ATTACH = 1001;
constexpr UINT IDM_QUIT = 1002;

struct TrayContext
{
    NOTIFYICONDATAW nid = {};
    bool icon_installed = false;
    std::function<void()> on_attach;
    std::function<void()> on_quit;
};

TrayContext* tray_context(HWND hwnd)
{
    return reinterpret_cast<TrayContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAY_ICON:
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU)
        {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, IDM_ATTACH, L"Attach Session");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, IDM_QUIT, L"Quit Draxul");
            // Required for the menu to dismiss when clicking elsewhere.
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            PostMessage(hwnd, WM_NULL, 0, 0);
        }
        else if (LOWORD(lParam) == WM_LBUTTONDBLCLK)
        {
            if (TrayContext* context = tray_context(hwnd); context && context->on_attach)
                context->on_attach();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_ATTACH:
            if (TrayContext* context = tray_context(hwnd); context && context->on_attach)
                context->on_attach();
            break;
        case IDM_QUIT:
            if (TrayContext* context = tray_context(hwnd); context && context->on_quit)
                context->on_quit();
            break;
        }
        return 0;

    case WM_DESTROY:
        if (TrayContext* context = tray_context(hwnd); context && context->icon_installed)
        {
            Shell_NotifyIconW(NIM_DELETE, &context->nid);
            context->icon_installed = false;
        }
        return 0;

    case WM_NCDESTROY:
        if (TrayContext* context = tray_context(hwnd))
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            delete context;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

bool ensure_tray_window_class()
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = tray_wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DraxulTrayWindow";
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND create_tray_message_window()
{
    if (!ensure_tray_window_class())
        return nullptr;
    return CreateWindowExW(0, L"DraxulTrayWindow", L"Draxul Tray",
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
}

void bind_tray_context(HWND hwnd, std::unique_ptr<TrayContext> context)
{
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(context.release()));
}

void clear_tray_callbacks(HWND hwnd)
{
    if (TrayContext* context = tray_context(hwnd))
    {
        // Make messages already queued for this HWND inert before window
        // destruction releases the instance-owned callback context.
        context->on_attach = {};
        context->on_quit = {};
    }
}

} // namespace

void* create_system_tray_icon(std::function<void()> on_attach, std::function<void()> on_quit)
{
    // Register a hidden message-only window class for tray icon messages.
    if (!ensure_tray_window_class())
    {
        DRAXUL_LOG_ERROR(LogCategory::Window, "Failed to register tray message window class");
        return nullptr;
    }

    HWND tray_hwnd = create_tray_message_window();
    if (!tray_hwnd)
    {
        DRAXUL_LOG_ERROR(LogCategory::Window, "Failed to create tray message window");
        return nullptr;
    }

    auto context = std::make_unique<TrayContext>();
    context->on_attach = std::move(on_attach);
    context->on_quit = std::move(on_quit);
    context->nid.cbSize = sizeof(context->nid);
    context->nid.hWnd = tray_hwnd;
    context->nid.uID = 1;
    context->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    context->nid.uCallbackMessage = WM_TRAY_ICON;
    // Use the application's main icon, falling back to a default.
    context->nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), L"IDI_ICON1");
    if (!context->nid.hIcon)
        context->nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512)); // IDI_APPLICATION
    wcscpy_s(context->nid.szTip, L"Draxul — sessions running");

    bind_tray_context(tray_hwnd, std::move(context));

    TrayContext* installed_context = tray_context(tray_hwnd);
    if (!installed_context || !Shell_NotifyIconW(NIM_ADD, &installed_context->nid))
    {
        DRAXUL_LOG_ERROR(LogCategory::Window, "Shell_NotifyIcon NIM_ADD failed");
        DestroyWindow(tray_hwnd);
        return nullptr;
    }
    installed_context->icon_installed = true;
    return tray_hwnd;
}

void destroy_system_tray_icon(void* raw_tray_icon)
{
    HWND tray_hwnd = static_cast<HWND>(raw_tray_icon);
    if (tray_hwnd && IsWindow(tray_hwnd))
    {
        clear_tray_callbacks(tray_hwnd);
        DestroyWindow(tray_hwnd);
    }
}

void pump_tray_messages(void* raw_tray_icon)
{
    HWND tray_hwnd = static_cast<HWND>(raw_tray_icon);
    if (!tray_hwnd || !IsWindow(tray_hwnd))
        return;
    MSG msg;
    while (PeekMessageW(&msg, tray_hwnd, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

namespace window_test
{

Win32TrayContextProbeResult exercise_win32_tray_context_lifetime()
{
    Win32TrayContextProbeResult result;

    HWND first = create_tray_message_window();
    result.first_window_created = first != nullptr;
    if (!first)
        return result;

    auto first_context = std::make_unique<TrayContext>();
    first_context->on_attach = [&result]() { ++result.first_attach_calls; };
    first_context->on_quit = [&result]() { ++result.first_quit_calls; };
    bind_tray_context(first, std::move(first_context));
    SendMessageW(first, WM_COMMAND, IDM_ATTACH, 0);

    // Simulate a message already queued when teardown starts. The context is
    // still installed, but its callbacks have been made inert.
    clear_tray_callbacks(first);
    SendMessageW(first, WM_COMMAND, IDM_ATTACH, 0);
    SendMessageW(first, WM_COMMAND, IDM_QUIT, 0);
    DestroyWindow(first);
    result.first_window_destroyed = !IsWindow(first);
    // A stale native handle must not re-enter the released first context.
    SendMessageW(first, WM_COMMAND, IDM_ATTACH, 0);

    HWND second = create_tray_message_window();
    result.second_window_created = second != nullptr;
    if (!second)
        return result;

    auto second_context = std::make_unique<TrayContext>();
    second_context->on_attach = [&result]() { ++result.second_attach_calls; };
    second_context->on_quit = [&result]() { ++result.second_quit_calls; };
    bind_tray_context(second, std::move(second_context));
    SendMessageW(second, WM_COMMAND, IDM_ATTACH, 0);
    SendMessageW(second, WM_COMMAND, IDM_QUIT, 0);
    clear_tray_callbacks(second);
    DestroyWindow(second);
    result.second_window_destroyed = !IsWindow(second);
    return result;
}

} // namespace window_test

} // namespace draxul

#endif // _WIN32
