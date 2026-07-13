#include "support/test_support.h"

#include <draxul/grid.h>
#include <draxul/log.h>

#include <catch2/catch_all.hpp>

using namespace draxul;
using namespace draxul::tests;

// ---------------------------------------------------------------------------
// Grid OOB / bounds safety tests (work item #05)
// ---------------------------------------------------------------------------

TEST_CASE("set_cell with OOB coordinates is a no-op", "[grid][oob]")
{
    ScopedLogCapture cap;
    Grid grid;
    grid.resize(4, 3);
    grid.clear_dirty();

    SECTION("negative col")
    {
        grid.set_cell(-1, 0, "X", 1, false);
        REQUIRE(grid.dirty_cell_count() == 0);
    }
    SECTION("col >= cols")
    {
        grid.set_cell(4, 0, "X", 1, false);
        REQUIRE(grid.dirty_cell_count() == 0);
    }
    SECTION("negative row")
    {
        grid.set_cell(0, -1, "X", 1, false);
        REQUIRE(grid.dirty_cell_count() == 0);
    }
    SECTION("row >= rows")
    {
        grid.set_cell(0, 3, "X", 1, false);
        REQUIRE(grid.dirty_cell_count() == 0);
    }
    SECTION("both out of range")
    {
        grid.set_cell(100, 100, "X", 1, false);
        REQUIRE(grid.dirty_cell_count() == 0);
    }
}

TEST_CASE("get_cell with OOB coordinates returns empty cell", "[grid][oob]")
{
    Grid grid;
    grid.resize(4, 3);
    grid.set_cell(0, 0, "A", 1, false);

    SECTION("negative col")
    {
        const auto& c = grid.get_cell(-1, 0);
        REQUIRE(c.text.empty());
        REQUIRE(c.hl_attr_id == 0);
    }
    SECTION("col >= cols")
    {
        const auto& c = grid.get_cell(4, 0);
        REQUIRE(c.text.empty());
    }
    SECTION("negative row")
    {
        const auto& c = grid.get_cell(0, -5);
        REQUIRE(c.text.empty());
    }
    SECTION("both extreme")
    {
        const auto& c = grid.get_cell(INT_MAX, INT_MAX);
        REQUIRE(c.text.empty());
    }
}

TEST_CASE("is_dirty with OOB coordinates returns false", "[grid][oob]")
{
    Grid grid;
    grid.resize(4, 3);
    grid.mark_all_dirty();

    REQUIRE_FALSE(grid.is_dirty(-1, 0));
    REQUIRE_FALSE(grid.is_dirty(0, -1));
    REQUIRE_FALSE(grid.is_dirty(4, 0));
    REQUIRE_FALSE(grid.is_dirty(0, 3));
}

TEST_CASE("mark_dirty with OOB coordinates is a no-op", "[grid][oob]")
{
    Grid grid;
    grid.resize(4, 3);
    grid.clear_dirty();

    grid.mark_dirty(-1, 0);
    grid.mark_dirty(0, -1);
    grid.mark_dirty(100, 100);
    REQUIRE(grid.dirty_cell_count() == 0);
}

TEST_CASE("scroll with OOB region is rejected", "[grid][oob]")
{
    ScopedLogCapture cap(LogLevel::Warn);
    Grid grid;
    grid.resize(10, 10);
    grid.set_cell(0, 0, "A", 0, false);
    grid.clear_dirty();

    SECTION("top >= bot")
    {
        grid.scroll(5, 5, 0, 10, 1);
        REQUIRE(grid.dirty_cell_count() == 0);
    }
    SECTION("negative top")
    {
        grid.scroll(-1, 10, 0, 10, 1);
        REQUIRE(grid.dirty_cell_count() == 0);
    }
    SECTION("bot > rows")
    {
        grid.scroll(0, 100, 0, 10, 1);
        REQUIRE(grid.dirty_cell_count() == 0);
    }
    SECTION("left >= right")
    {
        grid.scroll(0, 10, 5, 5, 1);
        REQUIRE(grid.dirty_cell_count() == 0);
    }
}

TEST_CASE("scroll with extreme delta is clamped", "[grid][oob]")
{
    ScopedLogCapture cap(LogLevel::Warn);
    Grid grid;
    grid.resize(10, 10);

    // Fill row 0 with identifiable content
    for (int c = 0; c < 10; c++)
        grid.set_cell(c, 0, std::string(1, 'A' + c), 0, false);
    grid.clear_dirty();

    // Scroll up by extreme amount — should clamp to region height and blank everything
    grid.scroll(0, 10, 0, 10, 10000);
    // All cells in region should be blank (scroll clamped to region size)
    for (int r = 0; r < 10; r++)
        for (int c = 0; c < 10; c++)
            REQUIRE(grid.get_cell(c, r).text == " ");
}

TEST_CASE("resize with pathological dimensions clamps", "[grid][oob]")
{
    ScopedLogCapture cap(LogLevel::Warn);
    Grid grid;

    SECTION("negative dimensions clamp to 0")
    {
        grid.resize(-5, -10);
        REQUIRE(grid.cols() == 0);
        REQUIRE(grid.rows() == 0);
    }
    SECTION("excessive columns clamp independently")
    {
        grid.resize(100000, 1);
        REQUIRE(grid.cols() == 10000);
        REQUIRE(grid.rows() == 1);
    }
    SECTION("excessive rows clamp independently")
    {
        grid.resize(1, 100000);
        REQUIRE(grid.cols() == 1);
        REQUIRE(grid.rows() == 10000);
    }
    SECTION("zero dimensions are valid")
    {
        grid.resize(0, 0);
        REQUIRE(grid.cols() == 0);
        REQUIRE(grid.rows() == 0);
    }
}
