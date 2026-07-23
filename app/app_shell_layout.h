#pragma once

namespace draxul
{

inline constexpr int kDefaultSpaceSidebarColumns = 20;
inline constexpr int kMinSpaceSidebarColumns = 12;
inline constexpr int kMaxSpaceSidebarColumns = 48;
inline constexpr int kMinContentColumns = 20;
inline constexpr int kAppShellDividerWidth = 4;

struct AppShellRect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool operator==(const AppShellRect&) const = default;
};

[[nodiscard]] bool contains(const AppShellRect& rect, int x, int y);

struct AppShellLayoutInput
{
    int window_width = 0;
    int window_height = 0;
    int terminal_height = 0;
    int cell_width = 0;
    int cell_height = 0;
    int preferred_sidebar_columns = kDefaultSpaceSidebarColumns;
    bool show_sidebar = false;
    bool show_tab_bar = true;
    bool zoomed = false;
};

struct AppShellLayout
{
    AppShellRect window;
    AppShellRect work_area;
    AppShellRect sidebar;
    AppShellRect sidebar_divider;
    AppShellRect content;
    AppShellRect tab_bar;
    AppShellRect pane_root;
    AppShellRect diagnostics;
    int effective_sidebar_columns = 0;
    bool sidebar_visible = false;
    bool chrome_visible = true;
};

[[nodiscard]] AppShellLayout compute_app_shell_layout(const AppShellLayoutInput& input);

} // namespace draxul
