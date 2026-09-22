#include "support/replay_fixture.h"
#include "support/test_support.h"

#include <draxul/grid.h>
#include <draxul/log.h>
#include <draxul/nvim_ui.h>

#include <catch2/catch_all.hpp>

using namespace draxul;
using namespace draxul::tests;

// Helper: feed a single redraw event through a UiEventHandler wired to a Grid.
static void feed(UiEventHandler& handler, MpackValue event)
{
    handler.process_redraw({ std::move(event) });
}

TEST_CASE("grid_line with row way out of range does not crash", "[grid_line_bounds]")
{
    ScopedLogCapture cap;

    Grid grid;
    grid.resize(80, 24);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    // row = 9999 — far beyond the 24-row grid
    feed(handler, redraw_event("grid_line", { grid_line_batch(1, 9999, 0, { cell("X", 1) }) }));

    // No crash means success. Also verify the grid is not corrupted.
    INFO("grid still has the correct number of columns after OOB row");
    REQUIRE(grid.cols() == 80);
    INFO("grid still has the correct number of rows after OOB row");
    REQUIRE(grid.rows() == 24);
    // The cell at (0,0) should remain the blank the grid was initialized with.
    INFO("OOB row event does not write to any valid cell");
    REQUIRE(grid.get_cell(0, 0).text == std::string(" "));

    // A WARN must have been emitted.
    bool found_warn = false;
    for (const auto& rec : cap.records)
    {
        if (rec.level == LogLevel::Warn)
        {
            found_warn = true;
            break;
        }
    }
    INFO("WARN log is emitted for OOB row coordinate");
    REQUIRE(found_warn);
}

TEST_CASE("grid_line with col_start out of range does not crash", "[grid_line_bounds]")
{
    ScopedLogCapture cap;

    Grid grid;
    grid.resize(80, 24);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    // col_start = 9999 — far beyond the 80-column grid
    feed(handler, redraw_event("grid_line", { grid_line_batch(1, 0, 9999, { cell("X", 1) }) }));

    INFO("grid still has the correct number of columns after OOB col_start");
    REQUIRE(grid.cols() == 80);
    INFO("grid still has the correct number of rows after OOB col_start");
    REQUIRE(grid.rows() == 24);

    // A WARN must have been emitted.
    bool found_warn = false;
    for (const auto& rec : cap.records)
    {
        if (rec.level == LogLevel::Warn)
        {
            found_warn = true;
            break;
        }
    }
    INFO("WARN log is emitted for OOB col_start coordinate");
    REQUIRE(found_warn);
}

TEST_CASE("grid_line with negative row does not crash", "[grid_line_bounds]")
{
    ScopedLogCapture cap;

    Grid grid;
    grid.resize(80, 24);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    // row = -1 — negative value
    feed(handler, redraw_event("grid_line", { grid_line_batch(1, -1, 0, { cell("X", 1) }) }));

    INFO("grid still has the correct dimensions after negative row");
    REQUIRE(grid.cols() == 80);
    REQUIRE(grid.rows() == 24);

    bool found_warn = false;
    for (const auto& rec : cap.records)
    {
        if (rec.level == LogLevel::Warn)
        {
            found_warn = true;
            break;
        }
    }
    INFO("WARN log is emitted for negative row coordinate");
    REQUIRE(found_warn);
}

TEST_CASE("grid_line with negative col_start does not crash", "[grid_line_bounds]")
{
    ScopedLogCapture cap;

    Grid grid;
    grid.resize(80, 24);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    // col_start = -5 — negative value
    feed(handler, redraw_event("grid_line", { grid_line_batch(1, 0, -5, { cell("X", 1) }) }));

    INFO("grid still has the correct dimensions after negative col_start");
    REQUIRE(grid.cols() == 80);
    REQUIRE(grid.rows() == 24);

    bool found_warn = false;
    for (const auto& rec : cap.records)
    {
        if (rec.level == LogLevel::Warn)
        {
            found_warn = true;
            break;
        }
    }
    INFO("WARN log is emitted for negative col_start coordinate");
    REQUIRE(found_warn);
}

TEST_CASE("grid_line with col_start near max int does not overflow", "[grid_line_bounds]")
{
    ScopedLogCapture cap;

    // Use a large col count to exercise the size_t arithmetic path in set_cell.
    // Use a moderate grid size (within the 10000 dimension cap).
    Grid grid;
    grid.resize(1000, 1);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    // col_start near INT_MAX — would overflow signed int if used as row*cols+col
    constexpr int kLargeCol = 0x7FFFFFFF; // INT_MAX
    feed(handler, redraw_event("grid_line", { grid_line_batch(1, 0, kLargeCol, { cell("X", 1) }) }));

    // grid dimensions unchanged, no crash.
    INFO("grid dimensions are unchanged after near-INT_MAX col_start");
    REQUIRE(grid.cols() == 1000);
    REQUIRE(grid.rows() == 1);

    // Either a WARN is emitted (if OOB check fires) or the cell is simply
    // silently dropped by set_cell's bounds check — either is acceptable.
    // Most important: the hostile input is handled without an invalid access.
}

TEST_CASE("valid grid_line events still work after bounds-checking additions", "[grid_line_bounds]")
{
    Grid grid;
    grid.resize(80, 24);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    feed(handler, redraw_event("grid_line", { grid_line_batch(1, 5, 3, { cell("A", 7), cell("B", 7) }) }));

    INFO("valid cell A lands at the correct position");
    REQUIRE(grid.get_cell(3, 5).text == std::string("A"));
    INFO("valid cell B lands at the correct position");
    REQUIRE(grid.get_cell(4, 5).text == std::string("B"));
}

TEST_CASE("grid_line with repeat count extending past right edge is clamped", "[grid_line_bounds]")
{
    Grid grid;
    grid.resize(10, 1);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    // col_start=7, cell "X" with repeat=10 → would write cols 7..16, but grid only has cols 0..9
    feed(handler, redraw_event("grid_line", { grid_line_batch(1, 0, 7, { cell("X", 1, 10) }) }));

    // Cells within bounds should be written
    INFO("cell at col 7 is written");
    REQUIRE(grid.get_cell(7, 0).text == std::string("X"));
    INFO("cell at col 8 is written");
    REQUIRE(grid.get_cell(8, 0).text == std::string("X"));
    INFO("cell at col 9 is written");
    REQUIRE(grid.get_cell(9, 0).text == std::string("X"));
    // No crash, no out-of-bounds access
    INFO("grid dimensions unchanged after repeat overrun");
    REQUIRE(grid.cols() == 10);
    REQUIRE(grid.rows() == 1);
}

TEST_CASE("grid_line with wide glyph at last column is handled safely", "[grid_line_bounds]")
{
    Grid grid;
    grid.resize(10, 1);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    // Place a double-width character starting at col 9 (last column) — the continuation
    // would land at col 10, which is out of bounds. Grid::set_cell handles this safely
    // by not writing the continuation when col+1 >= cols.
    // Use a CJK character (U+4E00 "一") which is double-width.
    feed(handler, redraw_event("grid_line", { grid_line_batch(1, 0, 9, { cell("\xe4\xb8\x80", 1) }) }));

    // The cell at col 9 should have the CJK character
    // Grid::set_cell clips the continuation silently when col+1 >= cols
    INFO("no crash on wide glyph at last column");
    REQUIRE(grid.cols() == 10);
    REQUIRE(grid.rows() == 1);
}

TEST_CASE("grid_line with combined repeat and wide glyph overrun", "[grid_line_bounds]")
{
    Grid grid;
    grid.resize(10, 1);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    // CJK character (2 cells each) starting at col 6, repeat=5
    // Would need cols: 6,7, 8,9, 10,11, 12,13, 14,15 — but grid is only 10 wide
    feed(handler, redraw_event("grid_line", { grid_line_batch(1, 0, 6, { cell("\xe4\xb8\x80", 1, 5) }) }));

    // First two CJK chars should land (cols 6-7, 8-9)
    INFO("first wide char lands at col 6");
    REQUIRE(grid.get_cell(6, 0).text == std::string("\xe4\xb8\x80"));
    INFO("no crash on combined repeat + wide overrun");
    REQUIRE(grid.cols() == 10);
    REQUIRE(grid.rows() == 1);
}

TEST_CASE("grid_line normal right-edge cell writes correctly", "[grid_line_bounds]")
{
    Grid grid;
    grid.resize(10, 1);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    // Fill exactly 10 columns: col_start=0, 10 cells with repeat=1
    feed(handler, redraw_event("grid_line", { grid_line_batch(1, 0, 0, { cell("A", 1), cell("B", 1), cell("C", 1), cell("D", 1), cell("E", 1), cell("F", 1), cell("G", 1), cell("H", 1), cell("I", 1), cell("J", 1) }) }));

    INFO("all 10 cells are written correctly");
    REQUIRE(grid.get_cell(0, 0).text == std::string("A"));
    REQUIRE(grid.get_cell(9, 0).text == std::string("J"));
}

TEST_CASE("grid_line with repeat filling exactly to right edge", "[grid_line_bounds]")
{
    Grid grid;
    grid.resize(10, 1);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    // col_start=5, "X" with repeat=5 → fills cols 5..9 exactly
    feed(handler, redraw_event("grid_line", { grid_line_batch(1, 0, 5, { cell("X", 1, 5) }) }));

    INFO("cell at col 5 is written");
    REQUIRE(grid.get_cell(5, 0).text == std::string("X"));
    INFO("cell at col 9 (last) is written");
    REQUIRE(grid.get_cell(9, 0).text == std::string("X"));
    // Cells before col 5 should be unchanged (blank)
    INFO("cells before col_start are unchanged");
    REQUIRE(grid.get_cell(4, 0).text == std::string(" "));
}
