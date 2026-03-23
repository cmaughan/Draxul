
#include <draxul/ui_panel.h>

#include <catch2/catch_all.hpp>

namespace
{

using namespace draxul;

void layout_hidden_uses_full_window_height()
{
    const PanelLayout layout = compute_panel_layout(1280, 800, 10, 20, 1, false);
    INFO("panel should be hidden");
    REQUIRE(layout.visible == false);
    INFO("hidden panel should not reserve height");
    REQUIRE(layout.panel_height == 0);
    INFO("hidden panel should leave the full terminal height");
    REQUIRE(layout.terminal_height == 800);
    INFO("hidden panel should preserve full-width grid columns");
    REQUIRE(layout.grid_size.x == 127);
    INFO("hidden panel should preserve full-height grid rows");
    REQUIRE(layout.grid_size.y == 39);
}

void layout_visible_reserves_bottom_panel_space()
{
    const PanelLayout layout = compute_panel_layout(1280, 800, 10, 20, 1, true);
    // desired = lround(800/3) = 267, grid_rows=26, snapped = 1 + 26*20 = 521
    INFO("panel should be visible");
    REQUIRE(layout.visible == true);
    INFO("panel height should fill remaining pixels after grid-snapped terminal");
    REQUIRE(layout.panel_height == 279);
    INFO("panel should start exactly at the bottom of the last terminal row");
    REQUIRE(layout.panel_y == 521);
    INFO("terminal height should snap to where terminal content ends");
    REQUIRE(layout.terminal_height == 521);
    INFO("panel should not change grid columns");
    REQUIRE(layout.grid_size.x == 127);
    INFO("panel should reduce the available grid rows");
    REQUIRE(layout.grid_size.y == 26);
    INFO("panel hit-testing should include bottom-region points");
    REQUIRE(layout.contains_panel_point(100, 600));
    INFO("panel hit-testing should exclude terminal points");
    REQUIRE(!layout.contains_panel_point(100, 500));
}

} // namespace

TEST_CASE("ui panel layout hidden uses full window height", "[ui]")
{
    layout_hidden_uses_full_window_height();
}

TEST_CASE("ui panel layout visible reserves bottom panel space", "[ui]")
{
    layout_visible_reserves_bottom_panel_space();
}
