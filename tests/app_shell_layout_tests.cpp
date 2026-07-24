#include "app_shell_layout.h"

#include <catch2/catch_test_macros.hpp>

using namespace draxul;

namespace
{
AppShellLayoutInput base_input()
{
    return {
        .window_width = 1200,
        .window_height = 800,
        .terminal_height = 700,
        .cell_width = 10,
        .cell_height = 20,
        .preferred_sidebar_columns = 20,
        .space_count = 2,
        .show_sidebar = true,
        .show_tab_bar = true,
    };
}
} // namespace

TEST_CASE("App shell partitions sidebar, tab bar, panes, and diagnostics", "[app_shell]")
{
    const auto layout = compute_app_shell_layout(base_input());

    CHECK(layout.sidebar == AppShellRect{ 0, 0, 200, 700 });
    CHECK(layout.sidebar_spaces == AppShellRect{ 0, 0, 200, 110 });
    CHECK(layout.sidebar_section_divider == AppShellRect{ 0, 110, 200, 1 });
    CHECK(layout.sidebar_agents == AppShellRect{ 0, 111, 200, 589 });
    CHECK(layout.sidebar_divider == AppShellRect{ 200, 0, 4, 700 });
    CHECK(layout.content == AppShellRect{ 204, 0, 996, 700 });
    CHECK(layout.tab_bar == AppShellRect{ 204, 0, 996, 22 });
    CHECK(layout.pane_root == AppShellRect{ 204, 22, 996, 678 });
    CHECK(layout.diagnostics == AppShellRect{ 0, 700, 1200, 100 });
    CHECK(layout.effective_sidebar_columns == 20);
}

TEST_CASE("App shell reserves an Agents heading when the Space list is tall", "[app_shell]")
{
    auto input = base_input();
    input.window_height = 140;
    input.terminal_height = 140;
    input.space_count = 20;
    const auto layout = compute_app_shell_layout(input);

    CHECK(layout.sidebar_section_divider == AppShellRect{ 0, 99, 200, 1 });
    CHECK(layout.sidebar_agents == AppShellRect{ 0, 100, 200, 40 });
}

TEST_CASE("App shell hides sidebar for one Space", "[app_shell]")
{
    auto input = base_input();
    input.show_sidebar = false;
    const auto layout = compute_app_shell_layout(input);

    CHECK_FALSE(layout.sidebar_visible);
    CHECK(layout.content == AppShellRect{ 0, 0, 1200, 700 });
    CHECK(layout.pane_root == AppShellRect{ 0, 22, 1200, 678 });
}

TEST_CASE("App shell shrinks sidebar first and snaps it to cells", "[app_shell]")
{
    auto input = base_input();
    input.window_width = 350;
    const auto layout = compute_app_shell_layout(input);

    CHECK(layout.sidebar.w == 140);
    CHECK(layout.effective_sidebar_columns == 14);
    CHECK(layout.content.w == 206);
    CHECK(layout.sidebar.w % input.cell_width == 0);
}

TEST_CASE("App shell clamps the preferred sidebar width", "[app_shell]")
{
    auto input = base_input();
    input.preferred_sidebar_columns = 100;
    CHECK(compute_app_shell_layout(input).effective_sidebar_columns == 48);

    input.preferred_sidebar_columns = 1;
    CHECK(compute_app_shell_layout(input).effective_sidebar_columns == 12);
}

TEST_CASE("App shell clamps zero and tiny dimensions without negative rectangles", "[app_shell]")
{
    auto input = base_input();
    input.window_width = -10;
    input.window_height = 8;
    input.terminal_height = 99;
    const auto layout = compute_app_shell_layout(input);

    CHECK(layout.window.w == 0);
    CHECK(layout.window.h == 8);
    CHECK(layout.work_area.h == 8);
    CHECK(layout.content.w == 0);
    CHECK(layout.pane_root.w == 0);
    CHECK(layout.pane_root.h >= 0);
    CHECK(layout.diagnostics.h == 0);
}

TEST_CASE("Zoom gives panes the full work area and hides chrome", "[app_shell]")
{
    auto input = base_input();
    input.zoomed = true;
    const auto layout = compute_app_shell_layout(input);

    CHECK_FALSE(layout.chrome_visible);
    CHECK_FALSE(layout.sidebar_visible);
    CHECK(layout.tab_bar.w == 0);
    CHECK(layout.pane_root == layout.work_area);
    CHECK(layout.diagnostics == AppShellRect{ 0, 700, 1200, 100 });
}

TEST_CASE("App shell rectangles use half-open hit testing", "[app_shell]")
{
    const AppShellRect rect{ 10, 20, 30, 40 };
    CHECK(contains(rect, 10, 20));
    CHECK(contains(rect, 39, 59));
    CHECK_FALSE(contains(rect, 40, 59));
    CHECK_FALSE(contains(rect, 39, 60));
}
