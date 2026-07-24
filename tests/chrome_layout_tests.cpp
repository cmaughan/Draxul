#include <catch2/catch_all.hpp>

#include "chrome_layout.h"

using namespace draxul;

namespace
{
void refresh_shell(ChromeLayoutInput& input, bool show_sidebar = false)
{
    input.shell_layout = compute_app_shell_layout({
        .window_width = input.viewport_width,
        .window_height = input.viewport_height,
        .terminal_height = input.viewport_height,
        .cell_width = input.cell_width,
        .cell_height = input.cell_height,
        .preferred_sidebar_columns = 20,
        .space_count = static_cast<int>(input.spaces.size()),
        .show_sidebar = show_sidebar,
    });
}

ChromeLayoutInput base_input()
{
    ChromeLayoutInput input;
    input.viewport_width = 800;
    input.viewport_height = 600;
    input.cell_width = 10;
    input.cell_height = 20;
    input.show_top_bar = true;
    input.tabs = { { 11, "alpha", true }, { 22, "beta", false } };
    refresh_shell(input);
    return input;
}
} // namespace

TEST_CASE("ChromeLayout golden tab structure remains stable", "[chrome][layout][golden]")
{
    const auto layout = compute_chrome_layout(base_input());
    REQUIRE(layout.bar_width == 800);
    REQUIRE(layout.bar_height == 22);
    REQUIRE(layout.grid_cols == 80);
    REQUIRE(layout.tabs.size() == 2);

    const auto& first = layout.tabs[0];
    CHECK(first.tab_id == 11);
    CHECK(first.tab_index == 1);
    CHECK(first.col_begin == 0);
    CHECK(first.col_end == 10);
    CHECK(first.text_col == 1);
    CHECK(first.label == "1: alpha");
    CHECK(first.rect.x == Catch::Approx(6.5f));
    CHECK(first.rect.y == Catch::Approx(2.0f));
    CHECK(first.rect.w == Catch::Approx(95.0f));
    CHECK(first.rect.h == Catch::Approx(16.0f));
    CHECK(first.accent_w == Catch::Approx(30.0f));

    const auto& second = layout.tabs[1];
    CHECK(second.tab_id == 22);
    CHECK(second.tab_index == 2);
    CHECK(second.col_begin == 10);
    CHECK(second.col_end == 19);
    CHECK(second.label == "2: beta");
    CHECK(hit_test_chrome(layout, ChromeHitKind::Tab, 103, 5) == 1);
    CHECK(hit_test_chrome(layout, ChromeHitKind::Tab, 104, 5) == 2);
}

TEST_CASE("ChromeLayout reserves a clickable Space sidebar", "[chrome][layout][spaces]")
{
    auto input = base_input();
    refresh_shell(input, true);
    input.spaces = {
        { 0, "default", true },
        { 7, "renderer", false },
    };
    input.agents = {
        { "agent-0-1-pane-2", "Codex", true, true },
        { "agent-7-3-pane-4", "Claude", false, false },
    };
    refresh_shell(input, true);

    const auto layout = compute_chrome_layout(input);
    REQUIRE(layout.content_x == 204);
    REQUIRE(layout.sidebar_width == 200);
    REQUIRE(layout.sidebar_cols == 20);
    CHECK(layout.sidebar_frame.x == Catch::Approx(8.0f));
    CHECK(layout.sidebar_frame.y == Catch::Approx(8.0f));
    CHECK(layout.sidebar_frame.w == Catch::Approx(190.0f));
    CHECK(layout.sidebar_frame.h == Catch::Approx(584.0f));
    CHECK(layout.sidebar_spaces_header.x == Catch::Approx(9.0f));
    CHECK(layout.sidebar_spaces_header.y == Catch::Approx(8.0f));
    CHECK(layout.sidebar_spaces_header.w == Catch::Approx(188.0f));
    CHECK(layout.sidebar_spaces_header.h == Catch::Approx(16.0f));
    CHECK(layout.sidebar_agents_header.x == Catch::Approx(9.0f));
    CHECK(layout.sidebar_agents_header.y == Catch::Approx(111.0f));
    CHECK(layout.sidebar_agents_header.w == Catch::Approx(188.0f));
    CHECK(layout.sidebar_agents_header.h == Catch::Approx(16.0f));
    CHECK(layout.sidebar_spaces_rect.y == Catch::Approx(0.0f));
    CHECK(layout.sidebar_spaces_rect.h == Catch::Approx(110.0f));
    CHECK(layout.sidebar_section_divider.x == Catch::Approx(8.0f));
    CHECK(layout.sidebar_section_divider.y == Catch::Approx(110.0f));
    CHECK(layout.sidebar_section_divider.w == Catch::Approx(190.0f));
    CHECK(layout.sidebar_section_divider.h == Catch::Approx(1.0f));
    CHECK(layout.sidebar_agents_rect.y == Catch::Approx(111.0f));
    REQUIRE(layout.bar_width == 596);
    REQUIRE(layout.grid_cols == 59);
    REQUIRE(layout.spaces.size() == 2);

    CHECK(layout.spaces[0].space_id == 0);
    CHECK(layout.spaces[0].row == 3);
    CHECK(layout.spaces[0].active);
    CHECK(layout.spaces[0].label == "1: default");
    CHECK(layout.spaces[0].accent_w == Catch::Approx(30.0f));
    CHECK(layout.spaces[0].rect.x == Catch::Approx(6.5f));
    CHECK(layout.spaces[0].rect.y == Catch::Approx(62.0f));
    CHECK(layout.spaces[0].rect.w == Catch::Approx(115.0f));
    CHECK(layout.spaces[0].rect.h == Catch::Approx(16.0f));
    CHECK(layout.spaces[0].palette.body_bg.r
        == Catch::Approx(input.theme.tab_inactive_bg.r));
    CHECK(layout.spaces[0].palette.accent_bg.r
        == Catch::Approx(input.theme.space_active_bg.r));
    CHECK(layout.spaces[1].space_id == 7);
    CHECK(layout.spaces[1].row == 4);
    CHECK_FALSE(layout.spaces[1].active);
    CHECK(layout.spaces[1].label == "2: renderer");
    CHECK(hit_test_chrome(layout, ChromeHitKind::Space, 10, 70) == 0);
    CHECK(hit_test_chrome(layout, ChromeHitKind::Space, 10, 90) == 7);
    CHECK(hit_test_chrome(layout, ChromeHitKind::Space, 210, 70) == -1);
    REQUIRE(layout.agents.size() == 2);
    CHECK(layout.agents[0].agent_index == 1);
    CHECK(layout.agents[0].instance_id == "agent-0-1-pane-2");
    CHECK(layout.agents[0].label == "1: Codex");
    CHECK(layout.agents[0].focused);
    CHECK(layout.agents[1].label == "2: Clau… [exited]");
    CHECK_FALSE(layout.agents[1].running);
    CHECK(hit_test_chrome(layout, ChromeHitKind::Agent, 10, 135) == 1);
    CHECK(hit_test_chrome(layout, ChromeHitKind::Agent, 10, 155) == 2);

    REQUIRE(layout.tabs.size() == 2);
    CHECK(layout.tabs[0].rect.x == Catch::Approx(210.5f));
    CHECK(layout.spaces[0].rect.x - layout.sidebar_rect.x
        == Catch::Approx(layout.tabs[0].rect.x - layout.content_x));
    CHECK(layout.spaces[0].rect.y
            - static_cast<float>(layout.spaces[0].row * layout.cell_height)
        == Catch::Approx(layout.tabs[0].rect.y));
    CHECK(layout.spaces[0].rect.h == Catch::Approx(layout.tabs[0].rect.h));
}

TEST_CASE("ChromeLayout keeps logical structure while DPI scales physical geometry",
    "[chrome][layout][golden][dpi]")
{
    auto input = base_input();
    const auto one_x = compute_chrome_layout(input);
    input.cell_width = 20;
    input.cell_height = 40;
    refresh_shell(input);
    const auto two_x = compute_chrome_layout(input);
    REQUIRE(two_x.tabs.size() == one_x.tabs.size());
    CHECK(two_x.tabs[0].col_begin == one_x.tabs[0].col_begin);
    CHECK(two_x.tabs[0].col_end == one_x.tabs[0].col_end);
    CHECK(two_x.tabs[0].label == one_x.tabs[0].label);
    CHECK(two_x.bar_height == 42);
    CHECK(two_x.tabs[0].rect.x == Catch::Approx(9.0f));
    CHECK(two_x.tabs[0].rect.w == Catch::Approx(190.0f));
    CHECK(hit_test_chrome(two_x, ChromeHitKind::Tab, 203, 39) == 1);
    CHECK(hit_test_chrome(two_x, ChromeHitKind::Tab, 204, 39) == 2);
}

TEST_CASE("ChromeLayout rename uses Unicode display columns and exposes caret geometry",
    "[chrome][layout][golden][rename][unicode]")
{
    auto input = base_input();
    input.rename.target = RenameTarget::Tab;
    input.rename.tab_id = 11;
    input.rename.buffer = "\xc3\x85\xe7\x95\x8c"; // one narrow plus one wide codepoint
    input.rename.cursor = input.rename.buffer.size();
    const auto layout = compute_chrome_layout(input);
    REQUIRE(layout.tabs.size() == 2);
    CHECK(layout.tabs[0].editing);
    CHECK(layout.tabs[0].label == "1: \xc3\x85\xe7\x95\x8c");
    // Rename minimum: prefix(3) + ten editable columns + two pad columns.
    CHECK(layout.tabs[0].col_end == 15);
    REQUIRE(layout.tab_caret);
    CHECK(layout.tab_caret->rect.x == Catch::Approx(74.0f));
    CHECK(layout.hit_regions[0].stable_id == 1);
    CHECK(layout.hit_regions[1].stable_id == 2);
}

TEST_CASE("ChromeLayout pane status structure degrades and keeps stable leaf hit ids",
    "[chrome][layout][golden][pane]")
{
    auto input = base_input();
    input.show_status = true;
    input.panes = {
        { 0, 22, 200, 200, 1, "a very long pane label", true, 41,
            Color{ 0.05f, 0.06f, 0.07f, 1.0f }, 7 },
        { 205, 22, 70, 200, 2, "shell", false, 42 },
    };
    const auto layout = compute_chrome_layout(input);
    REQUIRE(layout.pane_frames.size() == 2);
    CHECK(layout.pane_frames[0].leaf == 41);
    CHECK(layout.pane_frames[0].focused);
    CHECK(layout.pane_frames[0].outer.x == Catch::Approx(0.0f));
    CHECK(layout.pane_frames[0].outer.y == Catch::Approx(22.0f));
    CHECK(layout.pane_frames[0].rect.x == Catch::Approx(8.0f));
    CHECK(layout.pane_frames[0].rect.y == Catch::Approx(30.0f));
    CHECK(layout.pane_frames[0].rect.w == Catch::Approx(190.0f));
    CHECK(layout.pane_frames[0].rect.h == Catch::Approx(190.0f));
    CHECK(layout.pane_frames[0].content_tail.x == Catch::Approx(10.0f));
    CHECK(layout.pane_frames[0].content_tail.y == Catch::Approx(176.0f));
    CHECK(layout.pane_frames[0].content_tail.w == Catch::Approx(185.0f));
    CHECK(layout.pane_frames[0].content_tail.h == Catch::Approx(21.0f));
    CHECK(layout.pane_frames[1].leaf == 42);
    CHECK_FALSE(layout.pane_frames[1].focused);
    REQUIRE(layout.panes.size() == 2);
    CHECK(layout.panes[0].leaf == 41);
    CHECK(layout.panes[0].columns == 17);
    CHECK(layout.panes[0].label == "1: a very long\xe2\x80\xa6");
    CHECK_FALSE(layout.panes[0].number_only);
    CHECK(layout.panes[1].leaf == 42);
    CHECK(layout.panes[1].number_only);
    CHECK(layout.panes[1].label == "2:");
    const auto& hit = layout.hit_regions.back();
    CHECK(hit.kind == ChromeHitKind::PaneStatus);
    CHECK(hit.stable_id == 42);
    CHECK(hit.rect.w <= 50.0f);
}

TEST_CASE("ChromeLayout omits the focus accent when a tab has one visible pane",
    "[chrome][layout][pane][focus]")
{
    auto input = base_input();
    input.panes = {
        { 0, 22, 800, 578, 1, "shell", true, 41 },
    };

    const auto layout = compute_chrome_layout(input);

    REQUIRE(layout.pane_frames.size() == 1);
    CHECK_FALSE(layout.pane_frames[0].focused);
}

TEST_CASE("ChromeLayout keeps content clear of configurable pane strokes",
    "[chrome][layout][pane]")
{
    CHECK(pane_content_inset(0.0f) == 8);
    CHECK(pane_content_inset(3.0f) == 8);
    CHECK(pane_content_inset(10.0f) == 15);
    CHECK(pane_frame_line_inset(3.0f) == Catch::Approx(6.0f));
    CHECK(pane_content_edge_inset(3.0f, false) == 5);
    CHECK(pane_content_edge_inset(3.0f, true) == 10);
    CHECK(pane_frame_line_edge_inset(3.0f, false) == Catch::Approx(2.0f));
    CHECK(pane_frame_line_edge_inset(3.0f, true) == Catch::Approx(8.0f));
    CHECK(chrome_pill_height(20) == 16);
    CHECK(chrome_pill_band_height(20) == 20);
}

TEST_CASE("ChromeLayout adds margin at window edges without widening pane joins",
    "[chrome][layout][pane][split]")
{
    auto input = base_input();
    input.shell_layout.pane_root = { 0, 22, 404, 200 };
    input.panes = {
        { 0, 22, 200, 200, 1, "left", true, 41 },
        { 204, 22, 200, 200, 2, "right", false, 42 },
    };

    const auto layout = compute_chrome_layout(input);
    REQUIRE(layout.pane_frames.size() == 2);
    const auto& left = layout.pane_frames[0].rect;
    const auto& right = layout.pane_frames[1].rect;

    CHECK(left.x == Catch::Approx(8.0f));
    CHECK(left.x + left.w == Catch::Approx(198.0f));
    CHECK(right.x == Catch::Approx(206.0f));
    CHECK(right.x + right.w == Catch::Approx(396.0f));
    CHECK(right.x - (left.x + left.w) == Catch::Approx(8.0f));
}

TEST_CASE("ChromeLayout clips tabs before right-side status pills", "[chrome][layout][golden][clip]")
{
    auto input = base_input();
    input.viewport_width = 260;
    refresh_shell(input);
    SystemResourceSnapshot resources;
    resources.cpu_percent = 25;
    resources.memory_percent = 50;
    input.resources = resources;
    const auto layout = compute_chrome_layout(input);
    REQUIRE(layout.right_pills.size() == 1);
    CHECK(layout.right_pills[0].flat_right_edge);
    CHECK(layout.right_pills[0].rect.x >= 0.0f);
    CHECK(layout.right_pills[0].rect.x + layout.right_pills[0].rect.w
        == Catch::Approx(260.0f));
    for (const auto& tab : layout.tabs)
        CHECK(tab.col_end <= layout.right_pills[0].col_begin - 1);
}

TEST_CASE("ChromeLayout preserves one-tab geometry with weather and chord pills after resize",
    "[chrome][layout][golden][resize][weather]")
{
    auto input = base_input();
    input.viewport_width = 480;
    refresh_shell(input);
    input.tabs.resize(1);
    input.weather_emoji = "\xe2\x98\x80\xef\xb8\x8f";
    input.weather_temperature = "18\xc2\xb0\x43";
    input.chord = std::make_pair(std::string("Ctrl+K"), 0.75f);

    const auto layout = compute_chrome_layout(input);
    REQUIRE(layout.tabs.size() == 1);
    CHECK(layout.tabs[0].tab_id == 11);
    CHECK(layout.tabs[0].tab_index == 1);
    REQUIRE(layout.right_pills.size() == 2);
    // Text is stored as display clusters, matching the pre-extraction grid
    // writer: emoji + space + four temperature glyphs, then six chord glyphs.
    CHECK(layout.right_pills[0].clusters.size() == 6);
    CHECK(layout.right_pills[1].clusters.size() == 6);
    CHECK(layout.right_pills.front().col_end == layout.grid_cols);
    // The rounded weather pill retains the historical half-gap geometry; the
    // renderer viewport clips its 1.5px overshoot at the window edge.
    CHECK(layout.right_pills.front().rect.x + layout.right_pills.front().rect.w
        == Catch::Approx(481.5f));
    CHECK(layout.tabs[0].col_end <= layout.right_pills.back().col_begin - 1);
}
