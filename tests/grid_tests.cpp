#include "support/test_support.h"

#include <draxul/grid.h>
#include <draxul/log.h>
#include <draxul/unicode.h>

#include <catch2/catch_all.hpp>

using namespace draxul;
using namespace draxul::tests;

namespace
{

static bool is_valid_utf8(std::string_view s)
{
    size_t i = 0;
    while (i < s.size())
    {
        const uint8_t b = static_cast<uint8_t>(s[i]);
        size_t seq_len = 0;
        if (b < 0x80)
            seq_len = 1;
        else if ((b & 0xE0) == 0xC0)
            seq_len = 2;
        else if ((b & 0xF0) == 0xE0)
            seq_len = 3;
        else if ((b & 0xF8) == 0xF0)
            seq_len = 4;
        else
            return false;

        if (i + seq_len > s.size())
            return false;
        for (size_t j = 1; j < seq_len; ++j)
        {
            if ((static_cast<uint8_t>(s[i + j]) & 0xC0) != 0x80)
                return false;
        }
        i += seq_len;
    }
    return true;
}

} // namespace

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

TEST_CASE("grid detects http urls on dirty rows", "[grid][url]")
{
    Grid grid;
    grid.resize(32, 1);
    const std::string text = "go https://example.test/a";
    for (int col = 0; col < static_cast<int>(text.size()); ++col)
        grid.set_cell(col, 0, std::string(1, text[static_cast<size_t>(col)]), 0, false);

    grid.refresh_url_detection_for_dirty_rows(true);

    const int start = 3;
    const int end = static_cast<int>(text.size());
    for (int col = 0; col < start; ++col)
        REQUIRE(grid.effective_link_id(col, 0) == 0);
    for (int col = start; col < end; ++col)
    {
        REQUIRE(grid.effective_link_id(col, 0) != 0);
        REQUIRE(grid.link_uri(grid.effective_link_id(col, 0)) == "https://example.test/a");
    }
}

TEST_CASE("grid url detection does not overwrite explicit OSC hyperlink ids", "[grid][url]")
{
    Grid grid;
    grid.resize(24, 1);
    const uint16_t explicit_link = grid.link_id_for_uri("https://explicit.test");
    const std::string text = "https://visible.test";
    for (int col = 0; col < static_cast<int>(text.size()); ++col)
    {
        grid.set_cell(col, 0, std::string(1, text[static_cast<size_t>(col)]), 0, false);
        grid.set_cell_hyperlink_id(col, 0, explicit_link);
    }

    grid.refresh_url_detection_for_dirty_rows(true);

    REQUIRE(grid.effective_link_id(0, 0) == explicit_link);
    REQUIRE(grid.link_uri(grid.effective_link_id(0, 0)) == "https://explicit.test");
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

TEST_CASE("grid scroll down shifts rows toward bottom", "[grid][scroll]")
{
    Grid grid;
    grid.resize(3, 3);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            grid.set_cell(c, r, std::string(1, 'a' + r * 3 + c), 0, false);
    grid.clear_dirty();

    // Scroll down by 1: rows shift down, top row cleared
    grid.scroll(0, 3, 0, 3, -1);

    INFO("top row is cleared after scroll down");
    REQUIRE(grid.get_cell(0, 0).text == std::string(" "));
    REQUIRE(grid.get_cell(1, 0).text == std::string(" "));
    INFO("row 0 content moves to row 1");
    REQUIRE(grid.get_cell(0, 1).text == std::string("a"));
    REQUIRE(grid.get_cell(1, 1).text == std::string("b"));
    INFO("row 1 content moves to row 2");
    REQUIRE(grid.get_cell(0, 2).text == std::string("d"));
}

TEST_CASE("grid scroll right shifts columns toward right edge", "[grid][scroll]")
{
    Grid grid;
    grid.resize(5, 1);
    for (int c = 0; c < 5; ++c)
        grid.set_cell(c, 0, std::string(1, 'a' + c), 0, false);
    grid.clear_dirty();

    // Scroll right by 1 within full row: cols shift right, left col cleared
    grid.scroll(0, 1, 0, 5, 0, -1);

    INFO("left column is cleared after scroll right");
    REQUIRE(grid.get_cell(0, 0).text == std::string(" "));
    INFO("original col 0 content moves to col 1");
    REQUIRE(grid.get_cell(1, 0).text == std::string("a"));
    INFO("original col 1 content moves to col 2");
    REQUIRE(grid.get_cell(2, 0).text == std::string("b"));
    INFO("original col 3 content moves to col 4");
    REQUIRE(grid.get_cell(4, 0).text == std::string("d"));
}

TEST_CASE("grid scroll multi-row up shifts by correct amount", "[grid][scroll]")
{
    Grid grid;
    grid.resize(2, 5);
    for (int r = 0; r < 5; ++r)
        grid.set_cell(0, r, std::string(1, 'A' + r), 0, false);
    grid.clear_dirty();

    grid.scroll(0, 5, 0, 2, 2); // scroll up by 2

    INFO("row 2 moves to row 0");
    REQUIRE(grid.get_cell(0, 0).text == std::string("C"));
    INFO("row 3 moves to row 1");
    REQUIRE(grid.get_cell(0, 1).text == std::string("D"));
    INFO("row 4 moves to row 2");
    REQUIRE(grid.get_cell(0, 2).text == std::string("E"));
    INFO("bottom two rows are cleared");
    REQUIRE(grid.get_cell(0, 3).text == std::string(" "));
    REQUIRE(grid.get_cell(0, 4).text == std::string(" "));
}

TEST_CASE("grid scroll multi-row down shifts by correct amount", "[grid][scroll]")
{
    Grid grid;
    grid.resize(2, 5);
    for (int r = 0; r < 5; ++r)
        grid.set_cell(0, r, std::string(1, 'A' + r), 0, false);
    grid.clear_dirty();

    grid.scroll(0, 5, 0, 2, -2); // scroll down by 2

    INFO("top two rows are cleared");
    REQUIRE(grid.get_cell(0, 0).text == std::string(" "));
    REQUIRE(grid.get_cell(0, 1).text == std::string(" "));
    INFO("row 0 moves to row 2");
    REQUIRE(grid.get_cell(0, 2).text == std::string("A"));
    INFO("row 1 moves to row 3");
    REQUIRE(grid.get_cell(0, 3).text == std::string("B"));
    INFO("row 2 moves to row 4");
    REQUIRE(grid.get_cell(0, 4).text == std::string("C"));
}

TEST_CASE("grid scroll at top boundary of partial region", "[grid][scroll]")
{
    Grid grid;
    grid.resize(3, 5);
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 3; ++c)
            grid.set_cell(c, r, std::string(1, 'a' + r), 0, false);
    grid.clear_dirty();

    // Scroll only the top 3 rows (0-2), leaving rows 3-4 untouched
    grid.scroll(0, 3, 0, 3, 1);

    INFO("row 1 moves to row 0 within partial region");
    REQUIRE(grid.get_cell(0, 0).text == std::string("b"));
    INFO("row 2 moves to row 1");
    REQUIRE(grid.get_cell(0, 1).text == std::string("c"));
    INFO("bottom of region is cleared");
    REQUIRE(grid.get_cell(0, 2).text == std::string(" "));
    INFO("rows outside region are untouched");
    REQUIRE(grid.get_cell(0, 3).text == std::string("d"));
    REQUIRE(grid.get_cell(0, 4).text == std::string("e"));
}

TEST_CASE("grid scroll at bottom boundary of partial region", "[grid][scroll]")
{
    Grid grid;
    grid.resize(3, 5);
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 3; ++c)
            grid.set_cell(c, r, std::string(1, 'a' + r), 0, false);
    grid.clear_dirty();

    // Scroll only rows 2-4, leaving rows 0-1 untouched
    grid.scroll(2, 5, 0, 3, 1);

    INFO("rows outside region are untouched");
    REQUIRE(grid.get_cell(0, 0).text == std::string("a"));
    REQUIRE(grid.get_cell(0, 1).text == std::string("b"));
    INFO("row 3 moves to row 2 within partial region");
    REQUIRE(grid.get_cell(0, 2).text == std::string("d"));
    INFO("row 4 moves to row 3");
    REQUIRE(grid.get_cell(0, 3).text == std::string("e"));
    INFO("bottom of region is cleared");
    REQUIRE(grid.get_cell(0, 4).text == std::string(" "));
}

TEST_CASE("grid scroll wide-char boundary repair on scroll up", "[grid][scroll]")
{
    Grid grid;
    grid.resize(4, 3);
    // Place a wide char in row 2 that will scroll into the region
    grid.set_cell(0, 0, "a", 0, false);
    grid.set_cell(0, 1, "b", 0, false);
    grid.set_cell(0, 2, "W", 7, true); // wide char at col 0-1 of row 2
    grid.clear_dirty();

    grid.scroll(0, 3, 0, 4, 1);

    INFO("wide char scrolls up intact");
    REQUIRE(grid.get_cell(0, 1).text == std::string("W"));
    REQUIRE(grid.get_cell(0, 1).double_width);
    REQUIRE(grid.get_cell(1, 1).double_width_cont);
}

TEST_CASE("grid scroll wide-char boundary repair on scroll down", "[grid][scroll]")
{
    Grid grid;
    grid.resize(4, 3);
    grid.set_cell(0, 0, "W", 7, true); // wide char at col 0-1 of row 0
    grid.set_cell(0, 1, "b", 0, false);
    grid.set_cell(0, 2, "c", 0, false);
    grid.clear_dirty();

    grid.scroll(0, 3, 0, 4, -1);

    INFO("wide char scrolls down intact");
    REQUIRE(grid.get_cell(0, 1).text == std::string("W"));
    REQUIRE(grid.get_cell(0, 1).double_width);
    REQUIRE(grid.get_cell(1, 1).double_width_cont);
}

TEST_CASE("grid scroll multi-column left within partial horizontal region", "[grid][scroll]")
{
    Grid grid;
    grid.resize(6, 1);
    for (int c = 0; c < 6; ++c)
        grid.set_cell(c, 0, std::string(1, 'a' + c), 0, false);
    grid.clear_dirty();

    // Scroll columns 1-5 left by 2
    grid.scroll(0, 1, 1, 6, 0, 2);

    INFO("column outside region is untouched");
    REQUIRE(grid.get_cell(0, 0).text == std::string("a"));
    INFO("col 3 shifts to col 1");
    REQUIRE(grid.get_cell(1, 0).text == std::string("d"));
    INFO("col 4 shifts to col 2");
    REQUIRE(grid.get_cell(2, 0).text == std::string("e"));
    INFO("col 5 shifts to col 3");
    REQUIRE(grid.get_cell(3, 0).text == std::string("f"));
    INFO("right two columns are cleared");
    REQUIRE(grid.get_cell(4, 0).text == std::string(" "));
    REQUIRE(grid.get_cell(5, 0).text == std::string(" "));
}

// --- CellText truncation tests ---

TEST_CASE("CellText stores ASCII text correctly", "[celltext]")
{
    CellText ct;
    ct.assign("hello");
    REQUIRE(ct.view() == "hello");
    REQUIRE(ct.len == 5);
    REQUIRE_FALSE(ct.empty());
}

TEST_CASE("CellText stores empty string", "[celltext]")
{
    CellText ct;
    ct.assign("");
    REQUIRE(ct.view() == "");
    REQUIRE(ct.len == 0);
    REQUIRE(ct.empty());
}

TEST_CASE("CellText stores exactly kMaxLen bytes", "[celltext]")
{
    std::string exact(CellText::kMaxLen, 'X');
    CellText ct;
    ct.assign(exact);
    REQUIRE(ct.len == CellText::kMaxLen);
    REQUIRE(ct.view() == exact);
}

TEST_CASE("CellText truncates at kMaxLen and emits warning", "[celltext]")
{
    ScopedLogCapture cap;

    std::string over(CellText::kMaxLen + 1, 'Y');
    CellText ct;
    ct.assign(over);

    INFO("stored length is clamped to kMaxLen");
    REQUIRE(ct.len == CellText::kMaxLen);
    INFO("stored bytes match the first kMaxLen bytes of input");
    REQUIRE(ct.view() == over.substr(0, CellText::kMaxLen));

    bool found_warn = false;
    for (const auto& rec : cap.records)
    {
        if (rec.level == LogLevel::Warn)
        {
            found_warn = true;
            break;
        }
    }
    INFO("WARN is emitted for oversized cluster");
    REQUIRE(found_warn);
}

TEST_CASE("CellText truncation of multi-byte UTF-8 input", "[celltext]")
{
    ScopedLogCapture cap;

    // U+4E00 (一) = 0xE4 0xB8 0x80 (3 bytes each)
    // 11 chars = 33 bytes — one byte over the limit.
    std::string cjk;
    for (int i = 0; i < 11; i++)
        cjk += "\xe4\xb8\x80";
    REQUIRE(cjk.size() == 33);

    CellText ct;
    ct.assign(cjk);

    INFO("stored length is clamped to the largest valid UTF-8 prefix");
    REQUIRE(ct.len == 30);
    std::string_view stored = ct.view();
    INFO("stored bytes end on a UTF-8 codepoint boundary");
    REQUIRE(is_valid_utf8(stored));
    INFO("stored bytes match the largest valid prefix of input");
    REQUIRE(stored == std::string_view(cjk.data(), 30));
}

TEST_CASE("CellText ZWJ emoji sequence truncation", "[celltext]")
{
    ScopedLogCapture cap;

    // Family emoji: 👨‍👩‍👧‍👦 = 25 bytes. Two copies = 50 bytes.
    std::string family = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D"
                         "\xF0\x9F\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6";
    std::string two_families = family + family;
    REQUIRE(two_families.size() == 50);

    CellText ct;
    ct.assign(two_families);

    INFO("stored length is clamped to kMaxLen");
    REQUIRE(ct.len == CellText::kMaxLen);
    INFO("stored bytes remain valid UTF-8");
    REQUIRE(is_valid_utf8(ct.view()));
    INFO("stored bytes match the first 32 bytes of the ZWJ sequence");
    REQUIRE(ct.view() == std::string_view(two_families.data(), CellText::kMaxLen));

    bool found_warn = false;
    for (const auto& rec : cap.records)
    {
        if (rec.level == LogLevel::Warn)
        {
            found_warn = true;
            break;
        }
    }
    INFO("WARN is emitted for oversized ZWJ cluster");
    REQUIRE(found_warn);
}

TEST_CASE("CellText via Grid::set_cell warns on oversized cluster", "[celltext]")
{
    ScopedLogCapture cap;

    Grid grid;
    grid.resize(4, 1);

    std::string over(40, 'Z');
    grid.set_cell(0, 0, over, 0, false);

    const auto& cell = grid.get_cell(0, 0);
    INFO("cell text is truncated via Grid::set_cell");
    REQUIRE(cell.text.len == CellText::kMaxLen);

    bool found_warn = false;
    for (const auto& rec : cap.records)
    {
        if (rec.level == LogLevel::Warn)
        {
            found_warn = true;
            break;
        }
    }
    INFO("WARN is emitted through Grid::set_cell path");
    REQUIRE(found_warn);
}

TEST_CASE("grid clear sets full_dirty flag instead of per-cell push", "[grid]")
{
    Grid grid;
    grid.resize(80, 24);
    grid.clear_dirty();

    REQUIRE(grid.dirty_cell_count() == 0);
    REQUIRE(!grid.is_full_dirty());

    grid.clear();

    INFO("full_dirty flag is set after clear");
    REQUIRE(grid.is_full_dirty());
    INFO("dirty_cell_count reports total cell count");
    REQUIRE(grid.dirty_cell_count() == size_t(80 * 24));

    auto dirty = grid.get_dirty_cells();
    INFO("get_dirty_cells returns all positions when full_dirty");
    REQUIRE(dirty.size() == size_t(80 * 24));

    grid.clear_dirty();
    INFO("clear_dirty resets full_dirty flag");
    REQUIRE(!grid.is_full_dirty());
    REQUIRE(grid.dirty_cell_count() == 0);
}

TEST_CASE("grid mark_all_dirty sets full_dirty flag", "[grid]")
{
    Grid grid;
    grid.resize(10, 5);
    grid.clear_dirty();

    grid.mark_all_dirty();
    REQUIRE(grid.is_full_dirty());
    REQUIRE(grid.dirty_cell_count() == size_t(10 * 5));

    auto dirty = grid.get_dirty_cells();
    REQUIRE(dirty.size() == size_t(10 * 5));
}

TEST_CASE("grid resize updates dimensions and preserves in-bounds cells", "[grid]")
{
    Grid grid;
    grid.resize(5, 3);
    grid.set_cell(0, 0, "H", 1, false);
    grid.set_cell(4, 2, "W", 2, false);

    grid.resize(10, 6);

    REQUIRE(grid.cols() == 10);
    REQUIRE(grid.rows() == 6);
    REQUIRE(grid.get_cell(0, 0).text == std::string("H"));
    REQUIRE(grid.get_cell(4, 2).text == std::string("W"));

    grid.resize(2, 1);

    REQUIRE(grid.cols() == 2);
    REQUIRE(grid.rows() == 1);
    REQUIRE(grid.get_cell(0, 0).text == std::string("H"));
}

TEST_CASE("grid resize marks the resized grid fully dirty", "[grid]")
{
    Grid grid;
    grid.resize(10, 5);
    grid.clear_dirty();

    grid.resize(20, 8);

    REQUIRE(grid.is_full_dirty());
    REQUIRE(grid.dirty_cell_count() == size_t(20 * 8));
}
