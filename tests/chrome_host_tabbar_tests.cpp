// WI 119 — ChromeHost tab-bar hit-testing and viewport geometry.
//
// These tests pin down ChromeHost's pure layout surface — tab_bar_height(),
// hit_test_tab(), and how they respond to cell-size (DPI) changes — without
// standing up a real renderer, window, or Neovim process. Together with the
// existing chrome_host_rename_tests.cpp these give WI 125 (overlay registry
// refactor) a regression safety net around the chrome layout code.

#include <catch2/catch_all.hpp>

#include "chrome_host.h"
#include "pane_manager.h"
#include "space_controller.h"

#include "fake_grid_pipeline_renderer.h"

#include <memory>
#include <vector>

using namespace draxul;
using draxul::tests::FakeGridPipelineRenderer;

namespace
{
// Mirrors kGridPadding in chrome_host.cpp. hit_test_tab() shifts the tab
// column span by this constant when converting columns to pixels.
constexpr int kGridPadding = 4;

std::unique_ptr<Tab> make_test_tab(int id, std::string name)
{
    auto tab = std::make_unique<Tab>(id, PaneManager::Deps{});
    tab->name = std::move(name);
    return tab;
}

// Fixture that builds a ChromeHost wired to a FakeGridPipelineRenderer so
// tests can directly mutate cell_size_pixels() and observe tab_bar_height()
// / hit_test_tab() without a real GPU backend.
struct TabBarFixture
{
    SpaceController space_controller;
    FakeGridPipelineRenderer renderer;
    std::unique_ptr<ChromeHost> host;

    explicit TabBarFixture(std::initializer_list<std::pair<int, std::string>> ws_list)
    {
        for (const auto& [id, name] : ws_list)
            space_controller.active_tab_controller().tabs().push_back(make_test_tab(id, name));
        auto& tabs = space_controller.active_tab_controller();
        if (!tabs.empty())
            tabs.activate_tab(tabs.tabs().front()->id);

        renderer.cell_width_pixels = 10;
        renderer.cell_height_pixels = 20;

        ChromeHost::Deps deps;
        deps.space_controller = &space_controller;
        deps.grid_renderer = &renderer;
        host = std::make_unique<ChromeHost>(std::move(deps));
    }

    // Returns the pixel x-range [left, right) that tab at 0-based `index`
    // should occupy in the current renderer cell-size configuration. Mirrors
    // the layout math in ChromeHost::hit_test_tab().
    std::pair<int, int> expected_tab_px_range(size_t index) const
    {
        constexpr int kTabPadCols = 1;
        int col_cursor = 0;
        const auto& tabs = space_controller.active_tab_controller().tabs();
        for (size_t i = 0; i <= index && i < tabs.size(); ++i)
        {
            const std::string label = std::to_string(i + 1) + ": " + tabs[i]->name;
            const int total_cols = static_cast<int>(label.size()) + kTabPadCols * 2;
            if (i == index)
            {
                const int left = col_cursor * renderer.cell_width_pixels + kGridPadding;
                const int right = (col_cursor + total_cols) * renderer.cell_width_pixels + kGridPadding;
                return { left, right };
            }
            col_cursor += total_cols;
        }
        return { 0, 0 };
    }
};
} // namespace

TEST_CASE("ChromeHost tab_bar_height: follows renderer cell height", "[chrome_host][tabbar]")
{
    TabBarFixture f{ { 1, "alpha" } };
    // Default cell_height_pixels = 20; ChromeHost adds a 2px gap.
    REQUIRE(f.host->tab_bar_height() == 22);

    // Simulate a DPI / font-size bump. Cell height doubles → tab bar doubles
    // (plus the same constant 2px gap).
    f.renderer.cell_height_pixels = 40;
    REQUIRE(f.host->tab_bar_height() == 42);

    // A tiny cell also tracks linearly — no floor clamp today.
    f.renderer.cell_height_pixels = 10;
    REQUIRE(f.host->tab_bar_height() == 12);
}

TEST_CASE("ChromeHost tab_bar_height: hidden when no grid renderer", "[chrome_host][tabbar]")
{
    // Analogue of "chrome hidden" — no grid renderer means ChromeHost cannot
    // know a cell size and reports a 0-height tab bar, so App reserves no
    // vertical space for chrome.
    SpaceController space_controller;
    space_controller.active_tab_controller().tabs().push_back(make_test_tab(1, "solo"));
    space_controller.active_tab_controller().activate_tab(1);

    ChromeHost::Deps deps;
    deps.space_controller = &space_controller;
    deps.grid_renderer = nullptr;
    auto host = std::make_unique<ChromeHost>(std::move(deps));

    REQUIRE(host->tab_bar_height() == 0);
}

TEST_CASE("ChromeHost hit_test_tab: single tab hits anywhere across its span",
    "[chrome_host][tabbar][hittest]")
{
    TabBarFixture f{ { 1, "alpha" } };

    const auto [left, right] = f.expected_tab_px_range(0);
    // Sanity-check the layout math so the rest of the assertions are meaningful.
    // "1: alpha" = 8 chars; +2 padding cols = 10 cols * 10px = 100 px; +4 padding.
    REQUIRE(left == 4);
    REQUIRE(right == 104);

    const int py_mid = f.renderer.cell_height_pixels / 2;
    REQUIRE(f.host->hit_test_tab(left, py_mid) == 1);
    REQUIRE(f.host->hit_test_tab((left + right) / 2, py_mid) == 1);
    REQUIRE(f.host->hit_test_tab(right - 1, py_mid) == 1);

    // Just past the tab's right edge is a miss (no second tab to land in).
    REQUIRE(f.host->hit_test_tab(right, py_mid) == 0);
    // Well to the right — also a miss.
    REQUIRE(f.host->hit_test_tab(right + 200, py_mid) == 0);
}

TEST_CASE("ChromeHost exposes and hit-tests the Space sidebar when multiple Spaces exist",
    "[chrome_host][spaces][hittest]")
{
    TabBarFixture f{ { 1, "alpha" } };
    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    f.host->set_viewport(viewport);

    const SpaceId renderer = f.space_controller.create_space("renderer");
    REQUIRE(renderer != kInvalidSpaceId);
    REQUIRE(f.host->space_sidebar_width() == 200);

    CHECK(f.host->hit_test_space(10, 50) == kDefaultSpaceId);
    CHECK(f.host->hit_test_space(10, 70) == renderer);
    CHECK(f.host->hit_test_space(210, 50) == kInvalidSpaceId);

    const auto [tab_left, tab_right] = f.expected_tab_px_range(0);
    CHECK(f.host->hit_test_tab(tab_left, 10) == 0);
    CHECK(f.host->hit_test_tab(200 + tab_left, 10) == 1);
    CHECK(f.host->hit_test_tab(200 + tab_right, 10) == 0);
}

TEST_CASE("ChromeHost clamps the Space sidebar on narrow viewports", "[chrome_host][spaces]")
{
    TabBarFixture f{ { 1, "alpha" } };
    HostViewport viewport;
    viewport.pixel_size = { 300, 200 };
    f.host->set_viewport(viewport);
    REQUIRE(f.space_controller.create_space("renderer") != kInvalidSpaceId);

    CHECK(f.host->space_sidebar_width() == 100);
}

TEST_CASE("ChromeHost hit_test_tab: multiple tabs return correct 1-based index",
    "[chrome_host][tabbar][hittest]")
{
    TabBarFixture f{ { 1, "alpha" }, { 2, "beta" }, { 3, "gamma" } };

    const int py_mid = f.renderer.cell_height_pixels / 2;

    for (size_t i = 0; i < 3; ++i)
    {
        const auto [left, right] = f.expected_tab_px_range(i);
        const int px_mid = (left + right) / 2;
        REQUIRE(f.host->hit_test_tab(left, py_mid) == static_cast<int>(i) + 1);
        REQUIRE(f.host->hit_test_tab(px_mid, py_mid) == static_cast<int>(i) + 1);
        REQUIRE(f.host->hit_test_tab(right - 1, py_mid) == static_cast<int>(i) + 1);
    }

    // A gap between tab 1 and tab 2 should never exist (tabs are contiguous);
    // verify the boundary cleanly transitions instead of returning 0.
    const auto tab1 = f.expected_tab_px_range(0);
    const auto tab2 = f.expected_tab_px_range(1);
    REQUIRE(tab1.second == tab2.first);
    REQUIRE(f.host->hit_test_tab(tab1.second, py_mid) == 2);
}

TEST_CASE("ChromeHost hit_test_tab: clicks below the tab bar are rejected",
    "[chrome_host][tabbar][hittest]")
{
    TabBarFixture f{ { 1, "alpha" }, { 2, "beta" } };

    const int ch = f.renderer.cell_height_pixels;
    const auto [left, right] = f.expected_tab_px_range(0);
    const int px_mid = (left + right) / 2;

    // py == ch is just past the bottom row of the tab label region.
    REQUIRE(f.host->hit_test_tab(px_mid, ch) == 0);
    REQUIRE(f.host->hit_test_tab(px_mid, ch + 10) == 0);
    REQUIRE(f.host->hit_test_tab(px_mid, 1000) == 0);
    // Negative y is also rejected rather than crashing.
    REQUIRE(f.host->hit_test_tab(px_mid, -1) == 0);
}

TEST_CASE("ChromeHost hit_test_tab: clicks far outside window do not crash",
    "[chrome_host][tabbar][hittest]")
{
    TabBarFixture f{ { 1, "alpha" } };

    // Well beyond any realistic viewport. Must return 0 and not touch any
    // out-of-range layout state.
    REQUIRE(f.host->hit_test_tab(-1'000'000, 0) == 0);
    REQUIRE(f.host->hit_test_tab(1'000'000, 0) == 0);
    REQUIRE(f.host->hit_test_tab(0, -1'000'000) == 0);
    REQUIRE(f.host->hit_test_tab(0, 1'000'000) == 0);
}

TEST_CASE("ChromeHost hit_test_tab: empty tab list misses all clicks",
    "[chrome_host][tabbar][hittest]")
{
    // Zero tabs is an unusual transient state (e.g. between teardown
    // and fresh init). Hit-testing must never return a tab index in that
    // case.
    SpaceController space_controller;
    FakeGridPipelineRenderer renderer;
    renderer.cell_width_pixels = 10;
    renderer.cell_height_pixels = 20;

    ChromeHost::Deps deps;
    deps.space_controller = &space_controller;
    deps.grid_renderer = &renderer;
    auto host = std::make_unique<ChromeHost>(std::move(deps));

    REQUIRE(host->hit_test_tab(0, 0) == 0);
    REQUIRE(host->hit_test_tab(50, 10) == 0);
}

TEST_CASE("ChromeHost hit_test_tab: DPI scale doubles both hit regions and bar height",
    "[chrome_host][tabbar][hittest][dpi]")
{
    TabBarFixture f{ { 1, "alpha" }, { 2, "beta" } };

    // Baseline: capture the original right edge of tab 1.
    const auto [left_1x, right_1x] = f.expected_tab_px_range(0);
    const int mid_1x_tab1 = (left_1x + right_1x) / 2;
    REQUIRE(f.host->hit_test_tab(mid_1x_tab1, 5) == 1);
    const int bar_h_1x = f.host->tab_bar_height();

    // Simulate a DPI change: 2x cell size in both dimensions.
    f.renderer.cell_width_pixels = 20;
    f.renderer.cell_height_pixels = 40;

    // Tab bar height tracks cell height + 2px gap.
    REQUIRE(f.host->tab_bar_height() == bar_h_1x * 2 - 2); // (20+2) vs (40+2)
    REQUIRE(f.host->tab_bar_height() == 42);

    // The pixel coordinate that used to hit tab 1 now falls inside tab 1's
    // left half (since the tab grew). It's still on tab 1.
    REQUIRE(f.host->hit_test_tab(mid_1x_tab1, 10) == 1);

    // The new expected boundaries scale with cell width.
    const auto [left_2x, right_2x] = f.expected_tab_px_range(0);
    REQUIRE(left_2x == 4); // padding constant is DPI-independent
    REQUIRE(right_2x == 10 * 20 + 4); // 10 cols * 20 px + padding
    REQUIRE(f.host->hit_test_tab(right_2x - 1, 10) == 1);
    REQUIRE(f.host->hit_test_tab(right_2x, 10) == 2);

    // py must respect the scaled bar height. 25px was valid at 1x (< 40)
    // but 20 wasn't valid above ch=20 — verify the new ceiling works.
    REQUIRE(f.host->hit_test_tab(mid_1x_tab1, 39) == 1);
    REQUIRE(f.host->hit_test_tab(mid_1x_tab1, 40) == 0);
}

// WI 13 — ChromeHost used to measure tab widths by byte-length, so a UTF-8
// label like "Ångström" (9 bytes, 8 codepoints/columns) reserved 9 columns in
// the tab bar and mis-aligned every subsequent tab. Hit-testing on the right
// edge of the first tab would land in an imaginary phantom column past where
// the pill actually ends, and the second tab would be offset one cell to the
// right of its rendered pill. This test pins the codepoint-width behaviour.
TEST_CASE("ChromeHost hit_test_tab: UTF-8 label is measured in codepoints not bytes",
    "[chrome_host][tabbar][hittest][utf8]")
{
    // "Ångström" — the 'Å' is 2 bytes, the 'ö' is 2 bytes, so the raw
    // string is 9 bytes but only 8 display columns.
    TabBarFixture f{ { 1, "\xc3\x85ngstr\xc3\xb6m" }, { 2, "beta" } };

    // Label is "1: Ångström" = 11 columns. +2 padding = 13 cols * 10px = 130,
    // + 4 grid padding = left 4, right 134.
    constexpr int kTabPadCols = 1;
    const int tab1_cols = 11 + kTabPadCols * 2;
    const int tab1_left = 4;
    const int tab1_right = tab1_cols * 10 + 4;
    REQUIRE(tab1_right == 134);

    const int py = 5;
    // Right-edge pixel of tab 1's true column span must still hit tab 1.
    REQUIRE(f.host->hit_test_tab(tab1_right - 1, py) == 1);
    // One pixel past must fall into tab 2 (contiguous layout).
    REQUIRE(f.host->hit_test_tab(tab1_right, py) == 2);

    // The byte-count bug would have added a phantom column (1 extra 'ö' byte
    // + 1 extra 'Å' byte = 2 extra cols), placing tab2 at col 15 instead of
    // col 13, i.e. pixel 154 instead of 134. Verify a click at pixel 150 is
    // firmly inside tab 2 rather than being lost in a phantom tab-1 region.
    REQUIRE(f.host->hit_test_tab(150, py) == 2);
}

TEST_CASE("ChromeHost hit_test_tab: overflow-wide tab list still hit-tests correctly",
    "[chrome_host][tabbar][hittest]")
{
    // Many tabs with short names: hit_test_tab does not currently clip by
    // viewport width — it walks every tab and returns the first whose column
    // range contains the x. Verify that still behaves deterministically with
    // a large tab count.
    TabBarFixture f{
        { 1, "a" },
        { 2, "b" },
        { 3, "c" },
        { 4, "d" },
        { 5, "e" },
        { 6, "f" },
        { 7, "g" },
        { 8, "h" },
        { 9, "i" },
        { 10, "j" },
    };

    const int py = 5;
    for (size_t i = 0; i < 10; ++i)
    {
        const auto [left, right] = f.expected_tab_px_range(i);
        REQUIRE(f.host->hit_test_tab(left, py) == static_cast<int>(i) + 1);
        REQUIRE(f.host->hit_test_tab(right - 1, py) == static_cast<int>(i) + 1);
    }
}
