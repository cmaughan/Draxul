// Pane status pill layout — the shared geometry between ChromeHost::draw()'s
// NanoVG pill rect and update_pane_status_grids()'s text. Narrow panes must
// degrade full text → truncated text → number-only pill → hidden, and the
// pill may never exceed the pane (the original bug: an unclamped pill spilled
// its colored highlight into neighboring panes).

#include <catch2/catch_all.hpp>

#include "chrome_host.h"

using draxul::pane_status_pill_layout;

namespace
{
// Mirror the layout constants in chrome_host.cpp.
constexpr int kTabPadCols = 1; // padding cells each side of the label
constexpr int kPaneStatusRightMarginCols = 1;
constexpr int kEditMinNameCols = 10;
constexpr int CW = 10; // cell width used by these tests

int avail_cols(int pane_w_px)
{
    return pane_w_px / CW - kPaneStatusRightMarginCols;
}
} // namespace

TEST_CASE("pane status pill uses natural width when it fits", "[chrome]")
{
    // 1 digit, 20 text cols: pill = 2 pads + "N: " (3) + 20 = 25 cols.
    const auto layout = pane_status_pill_layout(400, CW, 1, 20, false);
    REQUIRE(layout.visible);
    CHECK_FALSE(layout.number_only);
    CHECK(layout.text_cols == 20);
    CHECK(layout.pill_cols == 2 * kTabPadCols + 3 + 20);
    CHECK(layout.pill_cols <= avail_cols(400));
}

TEST_CASE("pane status pill truncates text to the pane", "[chrome]")
{
    const auto layout = pane_status_pill_layout(200, CW, 1, 40, false);
    REQUIRE(layout.visible);
    CHECK_FALSE(layout.number_only);
    CHECK(layout.text_cols == avail_cols(200) - 2 * kTabPadCols - 3);
    CHECK(layout.text_cols < 40);
    CHECK(layout.pill_cols == avail_cols(200)); // fills exactly what fits
}

TEST_CASE("pane status pill degrades to number-only in narrow panes", "[chrome]")
{
    // avail = 5: tier 1 needs pads(2) + prefix(3) + 1 = 6 — too much; tier 2
    // needs pads(2) + digits(1) + 1 = 4 — fits.
    const auto layout = pane_status_pill_layout(60, CW, 1, 40, false);
    REQUIRE(layout.visible);
    CHECK(layout.number_only);
    CHECK(layout.text_cols == 0);
    CHECK(layout.pill_cols == 2 * kTabPadCols + 1 + 1);
    CHECK(layout.pill_cols <= avail_cols(60));

    // Two-digit pane index needs one more column.
    const auto two_digits = pane_status_pill_layout(60, CW, 2, 40, false);
    REQUIRE(two_digits.visible);
    CHECK(two_digits.number_only);
    CHECK(two_digits.pill_cols == 2 * kTabPadCols + 2 + 1);
}

TEST_CASE("pane status pill hides entirely when nothing fits", "[chrome]")
{
    CHECK_FALSE(pane_status_pill_layout(40, CW, 1, 40, false).visible); // avail 3 < 4
    CHECK_FALSE(pane_status_pill_layout(50, CW, 2, 40, false).visible); // avail 4 < 5
    CHECK_FALSE(pane_status_pill_layout(0, CW, 1, 5, false).visible);
    CHECK_FALSE(pane_status_pill_layout(400, 0, 1, 5, false).visible); // degenerate cell
    CHECK_FALSE(pane_status_pill_layout(400, CW, 0, 5, false).visible); // degenerate index
}

TEST_CASE("pane status pill keeps the rename field wide while editing", "[chrome]")
{
    // Empty buffer, editing: text portion grows to the edit minimum...
    const auto wide = pane_status_pill_layout(400, CW, 1, 0, true);
    REQUIRE(wide.visible);
    CHECK(wide.text_cols == kEditMinNameCols);

    // ...but still clamps to the pane like any other label.
    const auto clamped = pane_status_pill_layout(80, CW, 1, 0, true);
    REQUIRE(clamped.visible);
    CHECK_FALSE(clamped.number_only);
    CHECK(clamped.text_cols == avail_cols(80) - 2 * kTabPadCols - 3);
    CHECK(clamped.pill_cols <= avail_cols(80));
}
