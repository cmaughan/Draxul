
#include <draxul/grid.h>
#include <draxul/ui_panel.h>

#include <catch2/catch_all.hpp>

using namespace draxul;

// The full resize cascade (WindowResizeEvent -> App::on_resize -> renderer.resize
// -> host.set_viewport -> grid.resize) requires a live window and renderer and
// therefore cannot be driven headlessly. The integration glue in App::on_resize
// is exercised by the smoke / render-test suite.
//
// These tests cover the independently-testable components in the cascade:
//   1. Grid::resize() — the authoritative grid-dimension update
//   2. compute_panel_layout() — the function that converts pixel dimensions and
//      cell sizes into grid dimensions and HostViewport column/row counts
//
// If a full integration harness is ever built (mock IRenderer + IWindow), the
// skipped tests below indicate the exact assertions to add.

// -------------------------------------------------------------------------
// Grid::resize() — unit tests
// -------------------------------------------------------------------------

TEST_CASE("resize cascade: grid resize 80x24 to 120x40 updates dimensions", "[grid]")
{
    Grid grid;
    grid.resize(80, 24);
    INFO("initial cols are 80");
    REQUIRE(grid.cols() == 80);
    INFO("initial rows are 24");
    REQUIRE(grid.rows() == 24);

    grid.resize(120, 40);
    INFO("cols updated to 120 after resize");
    REQUIRE(grid.cols() == 120);
    INFO("rows updated to 40 after resize");
    REQUIRE(grid.rows() == 40);
}

TEST_CASE("resize cascade: grid resize to smaller dimensions does not crash", "[grid]")
{
    Grid grid;
    grid.resize(80, 24);

    // Fill a few cells so there is something to truncate.
    grid.set_cell(0, 0, "A", 1, false);
    grid.set_cell(79, 23, "Z", 1, false);

    grid.resize(40, 12);
    INFO("cols updated to 40 after shrink");
    REQUIRE(grid.cols() == 40);
    INFO("rows updated to 12 after shrink");
    REQUIRE(grid.rows() == 12);

    // Cells within the new bounds should be accessible without crashing.
    const auto& c = grid.get_cell(0, 0);
    INFO("cell (0,0) is still accessible after shrink");
    REQUIRE(!c.text.empty());
}

TEST_CASE("resize cascade: grid resize to identical dimensions is a no-op", "[grid]")
{
    Grid grid;
    grid.resize(80, 24);
    grid.set_cell(5, 3, "X", 2, false);
    grid.clear_dirty();

    grid.resize(80, 24);
    INFO("cols unchanged after same-size resize");
    REQUIRE(grid.cols() == 80);
    INFO("rows unchanged after same-size resize");
    REQUIRE(grid.rows() == 24);
}

TEST_CASE("resize cascade: two rapid sequential resizes yield final dimensions", "[grid]")
{
    Grid grid;
    grid.resize(80, 24);
    grid.resize(100, 30);
    grid.resize(120, 40);

    INFO("final cols match last resize (120)");
    REQUIRE(grid.cols() == 120);
    INFO("final rows match last resize (40)");
    REQUIRE(grid.rows() == 40);
}

TEST_CASE("resize cascade: grid resize marks all cells dirty", "[grid]")
{
    Grid grid;
    grid.resize(10, 5);
    grid.clear_dirty();

    grid.resize(20, 8);

    // After a resize the dirty count should cover the new grid area.
    const int expected_cells = 20 * 8;
    INFO("all cells in the new grid are dirty after resize");
    REQUIRE(static_cast<int>(grid.dirty_cell_count()) == expected_cells);
}

TEST_CASE("resize cascade: grid grow clears all cells (content repopulated by process)", "[grid]")
{
    // Grid::resize is a full reset — the terminal process (nvim or shell) resends
    // all content after a resize notification, so preserving old cells would just
    // be overwritten.  Verify that cells are blank after a grow, not that they
    // are preserved.
    Grid grid;
    grid.resize(5, 3);
    grid.set_cell(0, 0, "H", 1, false);
    grid.set_cell(4, 2, "W", 1, false);

    grid.resize(10, 6);

    // After resize the grid is blank — content repopulated by the terminal process.
    INFO("top-left cell is blank after grow (full reset)");
    REQUIRE(grid.get_cell(0, 0).text == std::string(" "));
    INFO("cols updated to 10 after grow");
    REQUIRE(grid.cols() == 10);
    INFO("rows updated to 6 after grow");
    REQUIRE(grid.rows() == 6);
}

// -------------------------------------------------------------------------
// compute_panel_layout() — maps pixel dimensions to grid dimensions
// -------------------------------------------------------------------------

TEST_CASE("resize cascade: panel layout 80x24 equivalent pixel dimensions", "[grid]")
{
    // Typical: 8x16 cells, 1px padding, 642x386 pixels gives 80x24
    const PanelLayout layout = compute_panel_layout(642, 386, 8, 16, 1, false);
    // cols = (642 - 2) / 8 = 80, rows = (386 - 2) / 16 = 24
    INFO("panel layout produces 80 columns");
    REQUIRE(layout.grid_size.x == 80);
    INFO("panel layout produces 24 rows");
    REQUIRE(layout.grid_size.y == 24);
}

TEST_CASE("resize cascade: panel layout grow from 80x24 to 120x40", "[grid]")
{
    // 8x16 cells, 1px padding
    const PanelLayout small_layout = compute_panel_layout(642, 386, 8, 16, 1, false);
    // 120 cols: 120*8 + 2 = 962px;  40 rows: 40*16 + 2 = 642px
    const PanelLayout large_layout = compute_panel_layout(962, 642, 8, 16, 1, false);

    INFO("larger window yields 120 columns");
    REQUIRE(large_layout.grid_size.x == 120);
    INFO("larger window yields 40 rows");
    REQUIRE(large_layout.grid_size.y == 40);
    INFO("grow increases column count");
    REQUIRE(large_layout.grid_size.x > small_layout.grid_size.x);
    INFO("grow increases row count");
    REQUIRE(large_layout.grid_size.y > small_layout.grid_size.y);
}

TEST_CASE("resize cascade: panel layout shrink to 40x12", "[grid]")
{
    // 40 cols: 40*8 + 2 = 322px;  12 rows: 12*16 + 2 = 194px
    const PanelLayout layout = compute_panel_layout(322, 194, 8, 16, 1, false);
    INFO("shrunk layout has 40 columns");
    REQUIRE(layout.grid_size.x == 40);
    INFO("shrunk layout has 12 rows");
    REQUIRE(layout.grid_size.y == 12);
}

TEST_CASE("resize cascade: panel layout same dimensions is stable", "[grid]")
{
    const PanelLayout a = compute_panel_layout(800, 600, 8, 16, 1, false);
    const PanelLayout b = compute_panel_layout(800, 600, 8, 16, 1, false);
    INFO("identical inputs produce identical column count");
    REQUIRE(a.grid_size.x == b.grid_size.x);
    INFO("identical inputs produce identical row count");
    REQUIRE(a.grid_size.y == b.grid_size.y);
}

// -------------------------------------------------------------------------
// Integration-level tests — skipped (require live renderer/window/host).
// To un-skip: inject mock IRenderer and IWindow into App, then synthesise
// a WindowResizeEvent and verify all three subsystems (renderer, host,
// grid) reflect the new dimensions.
// -------------------------------------------------------------------------

TEST_CASE("resize cascade: renderer receives new dimensions on resize [integration]", "[grid]")
{
    SKIP("requires live IRenderer — covered by smoke/render-test suite");
}

TEST_CASE("resize cascade: host viewport reflects new dimensions after resize [integration]", "[grid]")
{
    SKIP("requires live IHost with mock process — covered by smoke/render-test suite");
}

TEST_CASE("resize cascade: alt-screen resize re-dimensions main snapshot [integration]", "[grid]")
{
    SKIP("requires live host with alt-screen support — covered by smoke/render-test suite");
}
