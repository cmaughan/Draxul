#pragma once

namespace draxul::window_test
{

struct Win32TrayContextProbeResult
{
    bool first_window_created = false;
    bool first_window_destroyed = false;
    bool second_window_created = false;
    bool second_window_destroyed = false;
    int first_attach_calls = 0;
    int first_quit_calls = 0;
    int second_attach_calls = 0;
    int second_quit_calls = 0;
};

#ifdef _WIN32
// Exercises the production tray WndProc and GWLP_USERDATA ownership using
// message-only windows. No visible window or notification-area icon is made.
[[nodiscard]] Win32TrayContextProbeResult exercise_win32_tray_context_lifetime();
#endif

} // namespace draxul::window_test
