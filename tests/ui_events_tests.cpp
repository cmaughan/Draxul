#include "support/replay_fixture.h"

#include <draxul/grid.h>
#include <draxul/nvim.h>
#include <draxul/unicode.h>

#include <catch2/catch_all.hpp>

using namespace draxul;
using namespace draxul::tests;

namespace
{

class RecordingGridSink final : public IGridSink
{
public:
    struct SetCellCall
    {
        int col = 0;
        int row = 0;
        std::string text;
        uint16_t hl_id = 0;
        bool double_width = false;
    };

    struct ResizeCall
    {
        int cols = 0;
        int rows = 0;
    };

    struct ScrollCall
    {
        int top = 0;
        int bot = 0;
        int left = 0;
        int right = 0;
        int rows = 0;
        int cols = 0;
    };

    void resize(int cols, int rows) override
    {
        resize_calls.push_back({ cols, rows });
    }

    void clear() override
    {
        clear_count++;
    }

    void set_cell(int col, int row, const std::string& text, uint16_t hl_id, bool double_width) override
    {
        set_cell_calls.push_back({ col, row, text, hl_id, double_width });
    }

    void scroll(int top, int bot, int left, int right, int rows, int cols) override
    {
        scroll_calls.push_back({ top, bot, left, right, rows, cols });
    }

    std::vector<SetCellCall> set_cell_calls;
    std::vector<ResizeCall> resize_calls;
    std::vector<ScrollCall> scroll_calls;
    int clear_count = 0;
};

} // namespace

TEST_CASE("ui event handler replays redraw batches into the grid", "[ui]")
{
    Grid grid;
    grid.resize(4, 2);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    int flushes = 0;
    handler.on_flush = [&]() { ++flushes; };

    handler.process_redraw({ redraw_event("default_colors_set", { arr({ i(0xFFFFFF), i(0x101010), i(0xABCDEF) }) }),
        redraw_event("hl_attr_define", { arr({ i(7), map({ { s("foreground"), i(0x123456) }, { s("background"), i(0x654321) }, { s("bold"), b(true) } }) }) }),
        redraw_event("grid_line", { grid_line_batch(1, 0, 0, { cell("A", 7), cell("B", std::nullopt, 2) }) }),
        redraw_event("grid_cursor_goto", { arr({ i(1), i(1), i(2) }) }),
        redraw_event("mode_info_set", { arr({ b(true), arr({ map({ { s("name"), s("normal") }, { s("cursor_shape"), s("vertical") }, { s("cell_percentage"), i(25) }, { s("attr_id"), i(7) }, { s("blinkwait"), i(700) }, { s("blinkon"), i(400) }, { s("blinkoff"), i(250) } }) }) }) }),
        redraw_event("mode_change", { arr({ s("normal"), i(0) }) }),
        redraw_event("flush", {}) });

    INFO("flush callback fires once");
    REQUIRE(flushes == 1);
    INFO("first cell text is preserved");
    REQUIRE(grid.get_cell(0, 0).text == std::string("A"));
    INFO("first cell is written");
    REQUIRE(utf8_first_codepoint(grid.get_cell(0, 0).text.view()) == static_cast<uint32_t>('A'));
    INFO("repeat cell writes first copy");
    REQUIRE(utf8_first_codepoint(grid.get_cell(1, 0).text.view()) == static_cast<uint32_t>('B'));
    INFO("repeat cell writes second copy");
    REQUIRE(utf8_first_codepoint(grid.get_cell(2, 0).text.view()) == static_cast<uint32_t>('B'));
    INFO("cursor row is updated");
    REQUIRE(handler.cursor_row() == 1);
    INFO("cursor col is updated");
    REQUIRE(handler.cursor_col() == 2);
    INFO("mode index is updated");
    REQUIRE(handler.current_mode() == 0);
    INFO("mode table is populated");
    REQUIRE(static_cast<int>(handler.modes().size()) == 1);
    INFO("mode attr id is preserved");
    REQUIRE(handler.modes()[0].attr_id == 7);
    INFO("mode cell percentage is preserved");
    REQUIRE(handler.modes()[0].cell_percentage == 25);
    INFO("mode blinkwait is preserved");
    REQUIRE(handler.modes()[0].blinkwait == 700);
    INFO("mode blinkon is preserved");
    REQUIRE(handler.modes()[0].blinkon == 400);
    INFO("mode blinkoff is preserved");
    REQUIRE(handler.modes()[0].blinkoff == 250);
    INFO("highlight attributes are stored");
    REQUIRE(highlights.get(7).bold == true);
    INFO("default background is updated");
    REQUIRE(highlights.default_bg() == color_from_rgb(0x101010));
}

TEST_CASE("ui event handler forwards busy state transitions", "[ui]")
{
    UiEventHandler handler;
    bool busy = false;
    int transitions = 0;
    handler.on_busy = [&](bool value) {
        busy = value;
        transitions++;
    };

    handler.process_redraw({
        redraw_event("busy_start", {}),
        redraw_event("busy_stop", {}),
    });

    INFO("busy callbacks fire for both transitions");
    REQUIRE(transitions == 2);
    INFO("busy stop clears busy state");
    REQUIRE(busy == false);
}

TEST_CASE("ui event handler forwards mode changes immediately", "[ui]")
{
    UiEventHandler handler;
    int seen_mode = -1;
    handler.on_mode_change = [&](int mode) { seen_mode = mode; };

    handler.process_redraw({
        redraw_event("mode_change", { arr({ s("insert"), i(3) }) }),
    });

    INFO("mode index updates immediately");
    REQUIRE(handler.current_mode() == 3);
    INFO("mode change callback fires immediately");
    REQUIRE(seen_mode == 3);
}

TEST_CASE("ui event handler forwards grid resize callbacks", "[ui]")
{
    Grid grid;
    grid.resize(2, 2);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    int resized_cols = 0;
    int resized_rows = 0;
    handler.on_grid_resize = [&](int cols, int rows) {
        resized_cols = cols;
        resized_rows = rows;
    };

    handler.process_redraw({ redraw_event("grid_resize", { arr({ i(1), i(6), i(4) }) }) });

    INFO("grid resize updates columns");
    REQUIRE(grid.cols() == 6);
    INFO("grid resize updates rows");
    REQUIRE(grid.rows() == 4);
    INFO("resize callback receives columns");
    REQUIRE(resized_cols == 6);
    INFO("resize callback receives rows");
    REQUIRE(resized_rows == 4);
}

TEST_CASE("ui event handler can target a grid sink without the concrete grid type", "[ui]")
{
    RecordingGridSink sink;
    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&sink);
    handler.set_highlights(&highlights);

    handler.process_redraw({
        redraw_event("grid_line", { grid_line_batch(1, 3, 4, { cell("X", 9) }) }),
        redraw_event("grid_scroll", { arr({ i(1), i(0), i(4), i(1), i(8), i(1), i(0) }) }),
        redraw_event("grid_clear", { arr({ i(1) }) }),
        redraw_event("grid_resize", { arr({ i(1), i(10), i(6) }) }),
    });

    INFO("grid sink receives cell writes");
    REQUIRE(static_cast<int>(sink.set_cell_calls.size()) == 1);
    INFO("grid sink receives cell column");
    REQUIRE(sink.set_cell_calls[0].col == 4);
    INFO("grid sink receives cell row");
    REQUIRE(sink.set_cell_calls[0].row == 3);
    INFO("grid sink receives cell text");
    REQUIRE(sink.set_cell_calls[0].text == std::string("X"));
    INFO("grid sink receives highlight id");
    REQUIRE(sink.set_cell_calls[0].hl_id == static_cast<uint16_t>(9));
    INFO("grid sink receives scroll calls");
    REQUIRE(static_cast<int>(sink.scroll_calls.size()) == 1);
    INFO("grid sink receives scroll bounds");
    REQUIRE(sink.scroll_calls[0].bot == 4);
    INFO("grid sink receives horizontal scroll delta");
    REQUIRE(sink.scroll_calls[0].cols == 0);
    INFO("grid sink receives clear calls");
    REQUIRE(sink.clear_count == 1);
    INFO("grid sink receives resize calls");
    REQUIRE(static_cast<int>(sink.resize_calls.size()) == 1);
    INFO("grid sink receives resize columns");
    REQUIRE(sink.resize_calls[0].cols == 10);
    INFO("grid sink receives resize rows");
    REQUIRE(sink.resize_calls[0].rows == 6);
}

TEST_CASE("ui event handler forwards title updates", "[ui]")
{
    UiEventHandler handler;
    std::string title;
    handler.on_title = [&](const std::string& value) { title = value; };

    handler.process_redraw({
        redraw_event("set_title", { arr({ s("example.txt - Draxul") }) }),
    });

    INFO("title update is forwarded");
    REQUIRE(title == std::string("example.txt - Draxul"));
}

TEST_CASE("ui event handler ignores unknown redraw events", "[ui]")
{
    RecordingGridSink sink;
    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&sink);
    handler.set_highlights(&highlights);

    int flushes = 0;
    handler.on_flush = [&]() { ++flushes; };

    handler.process_redraw({
        redraw_event("totally_unknown", { arr({ i(1), i(2), i(3) }) }),
        redraw_event("flush", {}),
    });

    INFO("known events still dispatch after unknown redraw entries");
    REQUIRE(flushes == 1);
    INFO("unknown events do not touch the grid");
    REQUIRE(static_cast<int>(sink.set_cell_calls.size()) == 0);
    INFO("unknown events do not synthesize resize calls");
    REQUIRE(static_cast<int>(sink.resize_calls.size()) == 0);
    INFO("unknown events do not synthesize scroll calls");
    REQUIRE(static_cast<int>(sink.scroll_calls.size()) == 0);
    INFO("unknown events do not synthesize clears");
    REQUIRE(sink.clear_count == 0);
}

TEST_CASE("ui event handler applies grapheme widths like nvim", "[ui]")
{
    Grid grid;
    grid.resize(20, 1);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    const std::string combining = "e\xCC\x81";
    const std::string emoji = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD";
    const std::string flag = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
    const std::string family = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6";
    const std::string keycap = "1\xEF\xB8\x8F\xE2\x83\xA3";
    const std::string heart = "\xE2\x9D\xA4";
    const std::string heart_emoji = "\xE2\x9D\xA4\xEF\xB8\x8F";
    const std::string cjk = "\xE7\x95\x8C";
    const std::string devanagari = "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7";
    const std::string lazy = "\xF3\xB0\x92\xB2";
    const std::string devicon = "\xEE\x98\xA0";

    handler.process_redraw({ redraw_event("grid_line", { grid_line_batch(1, 0, 0, {
                                                                                      cell(combining, 1),
                                                                                      cell(emoji, 2),
                                                                                      cell(flag, 3),
                                                                                      cell(family, 4),
                                                                                      cell(keycap, 5),
                                                                                      cell(heart, 6),
                                                                                      cell(heart_emoji, 7),
                                                                                      cell(cjk, 8),
                                                                                      cell(devanagari, 9),
                                                                                      cell(lazy, 10),
                                                                                      cell(devicon, 11),
                                                                                  }) }) });

    INFO("combining cluster is preserved");
    REQUIRE(grid.get_cell(0, 0).text == combining);
    INFO("combining cluster remains single width");
    REQUIRE(grid.get_cell(0, 0).double_width == false);
    INFO("emoji cluster is preserved");
    REQUIRE(grid.get_cell(1, 0).text == emoji);
    INFO("emoji cluster is treated as double width");
    REQUIRE(grid.get_cell(1, 0).double_width == true);
    INFO("emoji continuation cell is marked");
    REQUIRE(grid.get_cell(2, 0).double_width_cont);
    INFO("flag lands after the emoji cluster");
    REQUIRE(grid.get_cell(3, 0).text == flag);
    INFO("flag is double width");
    REQUIRE(grid.get_cell(3, 0).double_width == true);
    INFO("flag continuation cell is marked");
    REQUIRE(grid.get_cell(4, 0).double_width_cont);
    INFO("family zwj sequence lands after the flag");
    REQUIRE(grid.get_cell(5, 0).text == family);
    INFO("family zwj sequence is double width");
    REQUIRE(grid.get_cell(5, 0).double_width == true);
    INFO("family continuation cell is marked");
    REQUIRE(grid.get_cell(6, 0).double_width_cont);
    INFO("keycap sequence remains single width");
    REQUIRE(grid.get_cell(7, 0).text == keycap);
    INFO("keycap sequence is single width");
    REQUIRE(grid.get_cell(7, 0).double_width == false);
    INFO("text heart remains single width");
    REQUIRE(grid.get_cell(8, 0).text == heart);
    INFO("text heart stays single width");
    REQUIRE(grid.get_cell(8, 0).double_width == false);
    INFO("emoji heart is preserved");
    REQUIRE(grid.get_cell(9, 0).text == heart_emoji);
    INFO("emoji heart becomes double width");
    REQUIRE(grid.get_cell(9, 0).double_width == true);
    INFO("emoji heart continuation cell is marked");
    REQUIRE(grid.get_cell(10, 0).double_width_cont);
    INFO("cjk glyph lands after emoji heart");
    REQUIRE(grid.get_cell(11, 0).text == cjk);
    INFO("cjk glyph remains double width");
    REQUIRE(grid.get_cell(11, 0).double_width == true);
    INFO("cjk continuation cell is marked");
    REQUIRE(grid.get_cell(12, 0).double_width_cont);
    INFO("indic conjunct remains single width");
    REQUIRE(grid.get_cell(13, 0).text == devanagari);
    INFO("indic conjunct stays single width");
    REQUIRE(grid.get_cell(13, 0).double_width == false);
    INFO("lazy icon remains single width");
    REQUIRE(grid.get_cell(14, 0).text == lazy);
    INFO("lazy icon stays single width");
    REQUIRE(grid.get_cell(14, 0).double_width == false);
    INFO("devicon remains single width");
    REQUIRE(grid.get_cell(15, 0).text == devicon);
    INFO("devicon stays single width");
    REQUIRE(grid.get_cell(15, 0).double_width == false);
}

TEST_CASE("ui event handler preserves alignment for empty-text repeated cells", "[ui]")
{
    Grid grid;
    grid.resize(8, 1);
    grid.set_cell(0, 0, "x", 1, false);
    grid.set_cell(1, 0, "y", 1, false);
    grid.set_cell(2, 0, "z", 1, false);
    grid.clear_dirty();

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    handler.process_redraw({
        redraw_event("grid_line", { grid_line_batch(1, 0, 0, {
                                                                 cell("A", 2),
                                                                 cell("", 3, 3),
                                                                 cell("B", 4),
                                                             }) }),
    });

    INFO("first cell is written");
    REQUIRE(grid.get_cell(0, 0).text == std::string("A"));
    INFO("empty-text cell clears the target column");
    REQUIRE(grid.get_cell(1, 0).text == std::string());
    INFO("repeat applies to the second cleared column");
    REQUIRE(grid.get_cell(2, 0).text == std::string());
    INFO("repeat applies to the third cleared column");
    REQUIRE(grid.get_cell(3, 0).text == std::string());
    INFO("subsequent cells stay aligned after repeats");
    REQUIRE(grid.get_cell(4, 0).text == std::string("B"));
}

TEST_CASE("ui event handler keeps alignment across odd repeat and empty cell combinations", "[ui]")
{
    Grid grid;
    grid.resize(8, 1);
    for (int col = 0; col < 8; ++col)
        grid.set_cell(col, 0, std::string(1, static_cast<char>('a' + col)), 1, false);
    grid.clear_dirty();

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    handler.process_redraw({
        redraw_event("grid_line", { grid_line_batch(1, 0, 0, {
                                                                 cell("", 2, 2),
                                                                 cell("skip-zero", 3, 0),
                                                                 cell("A", 4),
                                                                 cell("", 5, -2),
                                                                 cell("B", 6, 2),
                                                                 cell("", 7),
                                                                 cell("C", 8),
                                                             }) }),
    });

    INFO("empty repeats clear the first target column");
    REQUIRE(grid.get_cell(0, 0).text == std::string());
    INFO("empty repeats clear the second target column");
    REQUIRE(grid.get_cell(1, 0).text == std::string());
    INFO("empty repeats keep their highlight");
    REQUIRE(grid.get_cell(0, 0).hl_attr_id == static_cast<uint16_t>(2));
    INFO("empty repeats keep their highlight across repeats");
    REQUIRE(grid.get_cell(1, 0).hl_attr_id == static_cast<uint16_t>(2));
    INFO("zero repeats do not advance the column before later cells");
    REQUIRE(grid.get_cell(2, 0).text == std::string("A"));
    INFO("later cells keep their own highlight after skipped repeats");
    REQUIRE(grid.get_cell(2, 0).hl_attr_id == static_cast<uint16_t>(4));
    INFO("subsequent text stays aligned after skipped negative repeats");
    REQUIRE(grid.get_cell(3, 0).text == std::string("B"));
    INFO("valid repeats still expand after skipped repeats");
    REQUIRE(grid.get_cell(4, 0).text == std::string("B"));
    INFO("single empty cells still clear one column");
    REQUIRE(grid.get_cell(5, 0).text == std::string());
    INFO("single empty cells keep their highlight");
    REQUIRE(grid.get_cell(5, 0).hl_attr_id == static_cast<uint16_t>(7));
    INFO("later cells stay aligned after mixed repeat and empty cases");
    REQUIRE(grid.get_cell(6, 0).text == std::string("C"));
    INFO("cells beyond the batch stay untouched");
    REQUIRE(grid.get_cell(7, 0).text == std::string("h"));
}

TEST_CASE("ui event handler ignores malformed grid_line payloads", "[ui]")
{
    Grid grid;
    grid.resize(4, 1);
    grid.set_cell(0, 0, "x", 1, false);
    grid.set_cell(1, 0, "y", 1, false);
    grid.set_cell(2, 0, "z", 1, false);
    grid.set_cell(3, 0, "w", 1, false);
    grid.clear_dirty();

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    handler.process_redraw({
        arr({ i(42), arr({}) }),
        redraw_event("grid_line", { arr({ i(1), i(0), i(0) }) }),
        redraw_event("grid_line", { arr({ i(1), i(0), i(0), s("not-a-cell-array") }) }),
        redraw_event("grid_line", { arr({ i(1), i(0), i(0), arr({
                                                                i(7),
                                                                arr({ i(9) }),
                                                                arr({ s("A"), s("bad-hl") }),
                                                                arr({ s("B"), i(2), s("bad-repeat") }),
                                                                cell("C", 5),
                                                            }) }) }),
    });

    INFO("the first valid cell still lands");
    REQUIRE(grid.get_cell(0, 0).text == std::string("C"));
    INFO("malformed cells do not advance the column");
    REQUIRE(grid.get_cell(1, 0).text == std::string("y"));
    INFO("later cells remain untouched");
    REQUIRE(grid.get_cell(2, 0).text == std::string("z"));
    INFO("truncated batches are ignored");
    REQUIRE(grid.get_cell(3, 0).text == std::string("w"));
}

TEST_CASE("ui event handler consults option state for ambiwidth", "[ui]")
{
    Grid grid;
    grid.resize(4, 1);

    HighlightTable highlights;
    UiEventHandler handler;
    UiOptions options;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);
    handler.set_options(&options);
    handler.on_option_set = [&](const std::string& name, const MpackValue& value) {
        if (name == "ambiwidth" && value.type() == MpackValue::String)
            options.ambiwidth = (value.as_str() == "double") ? AmbiWidth::Double : AmbiWidth::Single;
    };

    const std::string ambiguous = "\xCE\xA9"; // Greek capital omega
    handler.process_redraw({
        redraw_event("option_set", { arr({ s("ambiwidth"), s("double") }) }),
        redraw_event("grid_line", { grid_line_batch(1, 0, 0, { cell(ambiguous, 1) }) }),
    });

    INFO("ambiguous-width glyph is written");
    REQUIRE(grid.get_cell(0, 0).text == ambiguous);
    INFO("ambiwidth=double makes ambiguous glyph double width");
    REQUIRE(grid.get_cell(0, 0).double_width == true);
    INFO("ambiguous-width continuation cell is marked");
    REQUIRE(grid.get_cell(1, 0).double_width_cont);
}

TEST_CASE("ui event handler does not crash when grid is null", "[ui]")
{
    HighlightTable highlights;
    UiEventHandler handler;
    // Deliberately NOT calling set_grid() — grid_ remains nullptr.
    handler.set_highlights(&highlights);

    int flushes = 0;
    handler.on_flush = [&]() { ++flushes; };

    // Send grid-touching events that would dereference grid_ if not guarded.
    handler.process_redraw({
        redraw_event("grid_line", { grid_line_batch(1, 0, 0, { cell("A", 1) }) }),
        redraw_event("grid_scroll", { arr({ i(1), i(0), i(4), i(0), i(8), i(2), i(0) }) }),
        redraw_event("grid_clear", { arr({ i(1) }) }),
        redraw_event("grid_resize", { arr({ i(1), i(10), i(6) }) }),
        redraw_event("flush", {}),
    });

    INFO("flush still fires even when grid is null");
    REQUIRE(flushes == 1);
}

TEST_CASE("ui event handler does not crash when both grid and highlights are null", "[ui]")
{
    UiEventHandler handler;
    // Deliberately NOT calling set_grid() or set_highlights().

    int flushes = 0;
    handler.on_flush = [&]() { ++flushes; };

    // Send events that touch grid_, highlights_, and neither.
    handler.process_redraw({
        redraw_event("grid_line", { grid_line_batch(1, 0, 0, { cell("X", 2) }) }),
        redraw_event("grid_clear", { arr({ i(1) }) }),
        redraw_event("grid_scroll", { arr({ i(1), i(0), i(4), i(0), i(8), i(1), i(0) }) }),
        redraw_event("grid_resize", { arr({ i(1), i(5), i(3) }) }),
        redraw_event("hl_attr_define", { arr({ i(7), map({ { s("bold"), b(true) } }) }) }),
        redraw_event("default_colors_set", { arr({ i(0xFFFFFF), i(0x000000), i(0xABCDEF) }) }),
        redraw_event("mode_change", { arr({ s("insert"), i(1) }) }),
        redraw_event("flush", {}),
    });

    INFO("flush fires when all sinks are null");
    REQUIRE(flushes == 1);
    INFO("mode change still updates internal state");
    REQUIRE(handler.current_mode() == 1);
}
