#include "support/test_support.h"

#include <draxul/grid.h>
#include <draxul/log.h>
#include <draxul/unicode.h>

#include <catch2/catch_all.hpp>

using namespace draxul;
using namespace draxul::tests;

TEST_CASE("grid tracks double-width continuations", "[grid]")
{
    Grid grid;
    grid.resize(4, 2);
    grid.clear_dirty();
    grid.set_cell(1, 0, "A", 7, true);

    const auto& cell = grid.get_cell(1, 0);
    const auto& cont = grid.get_cell(2, 0);
    INFO("double-width cell keeps text");
    REQUIRE(cell.text == std::string("A"));
    INFO("double-width cell keeps codepoint");
    REQUIRE(utf8_first_codepoint(cell.text.view()) == static_cast<uint32_t>('A'));
    INFO("double-width flag is set");
    REQUIRE(cell.double_width);
    INFO("continuation cell is marked");
    REQUIRE(cont.double_width_cont);
    INFO("continuation cell carries highlight");
    REQUIRE(cont.hl_attr_id == static_cast<uint16_t>(7));
}

TEST_CASE("grid scroll shifts rows and blanks the tail", "[grid]")
{
    Grid grid;
    grid.resize(3, 3);

    const char rows[3][3] = {
        { 'a', 'b', 'c' },
        { 'd', 'e', 'f' },
        { 'g', 'h', 'i' },
    };

    for (int row = 0; row < 3; ++row)
    {
        for (int col = 0; col < 3; ++col)
        {
            grid.set_cell(col, row, std::string(1, rows[row][col]), 0, false);
        }
    }

    grid.clear_dirty();
    grid.scroll(0, 3, 0, 3, 1);

    INFO("row 1 text moves into row 0");
    REQUIRE(grid.get_cell(0, 0).text == std::string("d"));
    INFO("row 1 moves into row 0");
    REQUIRE(utf8_first_codepoint(grid.get_cell(0, 0).text.view()) == static_cast<uint32_t>('d'));
    INFO("row 2 moves into row 1");
    REQUIRE(utf8_first_codepoint(grid.get_cell(2, 1).text.view()) == static_cast<uint32_t>('i'));
    INFO("scrolled-in row text is cleared");
    REQUIRE(grid.get_cell(1, 2).text == std::string(" "));
    INFO("scroll marks destination cells dirty");
    REQUIRE(grid.is_dirty(0, 0));
}

TEST_CASE("grid clears stale continuations when overwriting double-width cells", "[grid]")
{
    Grid grid;
    grid.resize(4, 1);

    grid.set_cell(1, 0, "X", 3, true);
    grid.set_cell(1, 0, "Y", 4, false);

    INFO("overwriting cell replaces text");
    REQUIRE(grid.get_cell(1, 0).text == std::string("Y"));
    INFO("overwriting cell clears double-width flag");
    REQUIRE(grid.get_cell(1, 0).double_width == false);
    INFO("continuation cell text is cleared");
    REQUIRE(grid.get_cell(2, 0).text == std::string());
    INFO("continuation marker is cleared");
    REQUIRE(grid.get_cell(2, 0).double_width_cont == false);
}

TEST_CASE("grid clears stale leaders when overwriting a continuation cell", "[grid]")
{
    Grid grid;
    grid.resize(4, 1);

    grid.set_cell(1, 0, "X", 3, true);
    grid.set_cell(2, 0, "Y", 4, false);

    INFO("former leader is cleared when its continuation is overwritten");
    REQUIRE(grid.get_cell(1, 0).text == std::string(" "));
    INFO("former leader clears the double-width flag");
    REQUIRE(grid.get_cell(1, 0).double_width == false);
    INFO("replacement text lands in the overwritten continuation column");
    REQUIRE(grid.get_cell(2, 0).text == std::string("Y"));
    INFO("replacement cell is no longer marked as a continuation");
    REQUIRE(grid.get_cell(2, 0).double_width_cont == false);
}

TEST_CASE("grid scroll preserves double-width cells and continuations together", "[grid]")
{
    Grid grid;
    grid.resize(4, 2);
    grid.set_cell(0, 1, "Z", 9, true);
    grid.clear_dirty();

    grid.scroll(0, 2, 0, 4, 1);

    INFO("double-width source cell scrolls into destination row");
    REQUIRE(grid.get_cell(0, 0).text == std::string("Z"));
    INFO("double-width flag scrolls with the cell");
    REQUIRE(grid.get_cell(0, 0).double_width == true);
    INFO("continuation cell scrolls with the source cell");
    REQUIRE(grid.get_cell(1, 0).double_width_cont);
    INFO("vacated row is cleared after scroll");
    REQUIRE(grid.get_cell(0, 1).text == std::string(" "));
}

TEST_CASE("grid scroll supports horizontal shifts within a partial region", "[grid]")
{
    Grid grid;
    grid.resize(5, 1);
    grid.set_cell(0, 0, "a", 1, false);
    grid.set_cell(1, 0, "b", 1, false);
    grid.set_cell(2, 0, "c", 1, false);
    grid.set_cell(3, 0, "d", 1, false);
    grid.set_cell(4, 0, "e", 1, false);
    grid.clear_dirty();

    grid.scroll(0, 1, 1, 5, 0, 1);

    INFO("cells outside the scrolled region stay unchanged");
    REQUIRE(grid.get_cell(0, 0).text == std::string("a"));
    INFO("horizontal scroll shifts region content left");
    REQUIRE(grid.get_cell(1, 0).text == std::string("c"));
    INFO("middle cells shift left");
    REQUIRE(grid.get_cell(2, 0).text == std::string("d"));
    INFO("tail cell shifts left");
    REQUIRE(grid.get_cell(3, 0).text == std::string("e"));
    INFO("newly uncovered cell is cleared");
    REQUIRE(grid.get_cell(4, 0).text == std::string(" "));
}

TEST_CASE("grid scroll preserves wide pairs fully inside a partial region", "[grid]")
{
    Grid grid;
    grid.resize(6, 1);
    grid.set_cell(0, 0, "a", 1, false);
    grid.set_cell(1, 0, "b", 1, false);
    grid.set_cell(2, 0, "W", 7, true);
    grid.set_cell(4, 0, "c", 1, false);
    grid.set_cell(5, 0, "z", 1, false);
    grid.clear_dirty();

    grid.scroll(0, 1, 1, 5, 0, 1);

    INFO("outside columns before the region stay unchanged");
    REQUIRE(grid.get_cell(0, 0).text == std::string("a"));
    INFO("wide leader shifts into the partial region");
    REQUIRE(grid.get_cell(1, 0).text == std::string("W"));
    INFO("wide leader keeps its flag when the pair stays in-region");
    REQUIRE(grid.get_cell(1, 0).double_width);
    INFO("continuation shifts with the leader when fully in-region");
    REQUIRE(grid.get_cell(2, 0).double_width_cont);
    INFO("trailing in-region text still shifts left");
    REQUIRE(grid.get_cell(3, 0).text == std::string("c"));
    INFO("outside columns after the region stay unchanged");
    REQUIRE(grid.get_cell(5, 0).text == std::string("z"));
}

TEST_CASE("grid scroll clears orphaned continuations at the left boundary without touching outside columns", "[grid]")
{
    Grid grid;
    grid.resize(5, 2);
    grid.set_cell(0, 0, "x", 1, false);
    grid.set_cell(1, 0, "y", 1, false);
    grid.set_cell(2, 0, "z", 1, false);
    grid.set_cell(3, 0, "u", 1, false);
    grid.set_cell(4, 0, "v", 1, false);
    grid.set_cell(0, 1, "W", 7, true);
    grid.set_cell(2, 1, "a", 1, false);
    grid.set_cell(3, 1, "b", 1, false);
    grid.set_cell(4, 1, "c", 1, false);
    grid.clear_dirty();

    grid.scroll(0, 2, 1, 5, 1);

    INFO("column left of the region is not clobbered");
    REQUIRE(grid.get_cell(0, 0).text == std::string("x"));
    INFO("orphaned continuation at the left boundary is cleared");
    REQUIRE(grid.get_cell(1, 0).text == std::string());
    INFO("cleared continuation resets the continuation flag");
    REQUIRE(grid.get_cell(1, 0).double_width_cont == false);
    INFO("in-region cells still scroll vertically");
    REQUIRE(grid.get_cell(2, 0).text == std::string("a"));
    INFO("middle cells keep their scrolled value");
    REQUIRE(grid.get_cell(3, 0).text == std::string("b"));
    INFO("tail cells keep their scrolled value");
    REQUIRE(grid.get_cell(4, 0).text == std::string("c"));
}

TEST_CASE("grid scroll does not clobber wide leaders outside a partial region", "[grid]")
{
    Grid grid;
    grid.resize(5, 1);
    grid.set_cell(0, 0, "W", 7, true);
    grid.set_cell(2, 0, "a", 1, false);
    grid.set_cell(3, 0, "b", 1, false);
    grid.set_cell(4, 0, "c", 1, false);
    grid.clear_dirty();

    grid.scroll(0, 1, 1, 5, 0, 1);

    INFO("leader outside the region stays untouched");
    REQUIRE(grid.get_cell(0, 0).text == std::string("W"));
    INFO("outside-region leader keeps its wide flag");
    REQUIRE(grid.get_cell(0, 0).double_width);
    INFO("scrolled text stays aligned after boundary repair");
    REQUIRE(grid.get_cell(1, 0).text == std::string("a"));
    INFO("middle cells continue to shift left");
    REQUIRE(grid.get_cell(2, 0).text == std::string("b"));
    INFO("tail cells continue to shift left");
    REQUIRE(grid.get_cell(3, 0).text == std::string("c"));
}

TEST_CASE("grid scroll clears leaders at the right boundary without touching outside continuations", "[grid]")
{
    Grid grid;
    grid.resize(5, 1);
    grid.set_cell(0, 0, "a", 1, false);
    grid.set_cell(1, 0, "b", 1, false);
    grid.set_cell(2, 0, "c", 1, false);
    grid.set_cell(3, 0, "W", 7, true);
    grid.clear_dirty();

    grid.scroll(0, 1, 0, 4, 0, 1);

    INFO("scrolled prefix still shifts left");
    REQUIRE(grid.get_cell(0, 0).text == std::string("b"));
    INFO("middle cells keep their shifted value");
    REQUIRE(grid.get_cell(1, 0).text == std::string("c"));
    INFO("orphaned leader is cleared after the shift");
    REQUIRE(grid.get_cell(2, 0).text == std::string(" "));
    INFO("cleared leader resets the double-width flag");
    REQUIRE(grid.get_cell(2, 0).double_width == false);
    INFO("continuation outside the region is left untouched");
    REQUIRE(grid.get_cell(4, 0).double_width_cont);
}

TEST_CASE("grid scroll full-width repair clears orphaned continuations", "[grid]")
{
    Grid grid;
    grid.resize(5, 1);
    grid.set_cell(0, 0, "W", 7, true);
    grid.set_cell(2, 0, "a", 1, false);
    grid.set_cell(3, 0, "b", 1, false);
    grid.set_cell(4, 0, "c", 1, false);
    grid.clear_dirty();

    grid.scroll(0, 1, 0, 5, 0, 1);

    INFO("full-width repair clears continuations orphaned at column zero");
    REQUIRE(grid.get_cell(0, 0).text == std::string());
    INFO("cleared continuation resets the continuation flag");
    REQUIRE(grid.get_cell(0, 0).double_width_cont == false);
    INFO("remaining cells still shift left");
    REQUIRE(grid.get_cell(1, 0).text == std::string("a"));
    INFO("middle cells keep their shifted value");
    REQUIRE(grid.get_cell(2, 0).text == std::string("b"));
    INFO("tail cells keep their shifted value");
    REQUIRE(grid.get_cell(3, 0).text == std::string("c"));
}

TEST_CASE("grid ignores double-width continuation when placed at the right edge", "[grid]")
{
    Grid grid;
    grid.resize(2, 1);

    grid.set_cell(1, 0, "X", 7, true);

    INFO("edge cell text is written");
    REQUIRE(grid.get_cell(1, 0).text == std::string("X"));
    INFO("edge cell keeps the double-width flag");
    REQUIRE(grid.get_cell(1, 0).double_width == true);
    INFO("neighboring cells are untouched");
    REQUIRE(grid.get_cell(0, 0).text == std::string(" "));
}

TEST_CASE("set_cell warns when cluster exceeds CellText::kMaxLen", "[grid]")
{
    ScopedLogCapture cap;
    std::string long_cluster(40, 'x');
    Grid grid;
    grid.resize(2, 1);
    grid.set_cell(0, 0, long_cluster, 0, false);
    bool found_warn = false;
    for (const auto& rec : cap.records)
    {
        if (rec.level == LogLevel::Warn && std::string_view(rec.message).find("truncat") != std::string_view::npos)
        {
            found_warn = true;
            break;
        }
    }
    INFO("expected a Warn log containing 'truncat' when cluster exceeds kMaxLen");
    REQUIRE(found_warn);
}

TEST_CASE("grid clear resets dirty bookkeeping for later cell updates", "[grid]")
{
    Grid grid;
    grid.resize(4, 2);
    grid.clear_dirty();

    grid.clear();
    grid.set_cell(0, 0, "X", 1, false);

    auto dirty = grid.get_dirty_cells();
    INFO("dirty list is repopulated after clear");
    REQUIRE(!dirty.empty());

    bool saw_origin = false;
    for (const auto& cell : dirty)
    {
        if (cell.col == 0 && cell.row == 0)
        {
            saw_origin = true;
            break;
        }
    }

    INFO("updated origin cell is present in dirty list");
    REQUIRE(saw_origin);
    INFO("updated cell text is preserved");
    REQUIRE(grid.get_cell(0, 0).text == std::string("X"));
}
