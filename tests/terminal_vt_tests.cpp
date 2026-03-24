#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"
#include "support/test_support.h"

#include <draxul/terminal_host_base.h>

#include <draxul/host.h>
#include <draxul/renderer.h>
#include <draxul/text_service.h>
#include <draxul/window.h>

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

// ---------------------------------------------------------------------------
// TestTerminalHost — exposes consume_output() as feed() for unit tests.
// ---------------------------------------------------------------------------

class TestTerminalHost final : public TerminalHostBase
{
public:
    // Feed raw VT bytes into the parser.
    void feed(std::string_view bytes)
    {
        consume_output(bytes);
    }

    // Grid inspection helpers.
    std::string cell_text(int col, int row)
    {
        return std::string(grid().get_cell(col, row).text.view());
    }
    uint16_t cell_hl(int col, int row)
    {
        return grid().get_cell(col, row).hl_attr_id;
    }

    int col() const
    {
        return cursor_col();
    }
    int row() const
    {
        return cursor_row();
    }
    int scroll_top() const
    {
        return vt_state().scroll_top;
    }
    int scroll_bottom() const
    {
        return vt_state().scroll_bottom;
    }

    std::string written; // bytes sent back to the "process"

    int cols_ = 20;
    int rows_ = 5;

protected:
    std::string_view host_name() const override
    {
        return "test";
    }

    bool initialize_host() override
    {
        highlights().set_default_fg({ 1.0f, 1.0f, 1.0f, 1.0f });
        highlights().set_default_bg({ 0.0f, 0.0f, 0.0f, 1.0f });
        apply_grid_size(cols_, rows_);
        reset_terminal_state();
        set_content_ready(true);
        return true;
    }

    bool do_process_write(std::string_view text) override
    {
        written += text;
        return true;
    }
    std::vector<std::string> do_process_drain() override
    {
        return {};
    }
    bool do_process_resize(int, int) override
    {
        return true;
    }
    bool do_process_is_running() const override
    {
        return true;
    }
    void do_process_shutdown() override {}
};

// ---------------------------------------------------------------------------
// Setup helper
// ---------------------------------------------------------------------------

struct TermSetup
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    TestTerminalHost host;
    TestHostCallbacks callbacks;
    bool ok = false;

    explicit TermSetup(int cols = 20, int rows = 5)
    {
        host.cols_ = cols;
        host.rows_ = rows;

        // TextService initialisation: don't care about glyph rendering for VT tests.
        // Use an absolute path so the test works regardless of the working directory.
        TextServiceConfig ts_cfg;
        ts_cfg.font_path = (std::filesystem::path(DRAXUL_PROJECT_ROOT) / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf").string();
        text_service.initialize(ts_cfg, TextService::DEFAULT_POINT_SIZE, 96.0f);

        HostViewport vp;
        vp.grid_size.x = cols;
        vp.grid_size.y = rows;

        HostContext ctx{ &window, &renderer, &text_service, {}, vp, 96.0f };
        ok = host.initialize(ctx, callbacks);
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("terminal: plain text writes to grid cells", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("Hello");
    INFO("col 0");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("H"));
    INFO("col 1");
    REQUIRE(ts.host.cell_text(1, 0) == std::string("e"));
    INFO("col 4");
    REQUIRE(ts.host.cell_text(4, 0) == std::string("o"));
    INFO("cursor at column 5 after 'Hello'");
    REQUIRE(ts.host.col() == 5);
}

TEST_CASE("terminal: carriage return moves cursor to column 0", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("AB\rC");
    INFO("CR overwrites from col 0");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("C"));
    INFO("col 1 unchanged");
    REQUIRE(ts.host.cell_text(1, 0) == std::string("B"));
    INFO("cursor at col 1 after C");
    REQUIRE(ts.host.col() == 1);
}

TEST_CASE("terminal: linefeed advances row without carriage return", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("AB\nC");
    INFO("row 0 col 0 preserved");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("A"));
    INFO("C at row 1 col 2");
    REQUIRE(ts.host.cell_text(2, 1) == std::string("C"));
    INFO("cursor at row 1");
    REQUIRE(ts.host.row() == 1);
}

TEST_CASE("terminal: CRLF advances row and resets column", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("AB\r\nCD");
    INFO("C at row 1 col 0");
    REQUIRE(ts.host.cell_text(0, 1) == std::string("C"));
    INFO("D at row 1 col 1");
    REQUIRE(ts.host.cell_text(1, 1) == std::string("D"));
    INFO("cursor at col 2");
    REQUIRE(ts.host.col() == 2);
    INFO("cursor at row 1");
    REQUIRE(ts.host.row() == 1);
}

TEST_CASE("terminal: scroll when cursor at bottom", "[terminal]")
{
    TermSetup ts(4, 3);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("A\r\nB\r\nC\r\nD");
    INFO("B scrolled to row 0");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("B"));
    INFO("C at row 1");
    REQUIRE(ts.host.cell_text(0, 1) == std::string("C"));
    INFO("D at row 2");
    REQUIRE(ts.host.cell_text(0, 2) == std::string("D"));
    INFO("cursor at bottom row");
    REQUIRE(ts.host.row() == 2);
}

TEST_CASE("terminal: CSI H positions cursor", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[4;7H");
    INFO("row 3 (0-based from 1-based 4)");
    REQUIRE(ts.host.row() == 3);
    INFO("col 6 (0-based from 1-based 7)");
    REQUIRE(ts.host.col() == 6);
}

TEST_CASE("terminal: CSI H with no params moves to home", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("AB\r\nCD\x1B[H");
    INFO("row 0 after home");
    REQUIRE(ts.host.row() == 0);
    INFO("col 0 after home");
    REQUIRE(ts.host.col() == 0);
}

TEST_CASE("terminal: CSI A/B/C/D move cursor", "[terminal]")
{
    TermSetup ts(20, 5);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[3;3H"); // 1-based row 3 col 3 -> row 2 col 2
    ts.host.feed("\x1B[2A");
    INFO("up 2 from row 2 = row 0");
    REQUIRE(ts.host.row() == 0);
    ts.host.feed("\x1B[3B");
    INFO("down 3 from row 0 = row 3");
    REQUIRE(ts.host.row() == 3);
    ts.host.feed("\x1B[5C");
    INFO("right 5 from col 2 = col 7");
    REQUIRE(ts.host.col() == 7);
    ts.host.feed("\x1B[3D");
    INFO("left 3 from col 7 = col 4");
    REQUIRE(ts.host.col() == 4);
}

TEST_CASE("terminal: CSI G CHA positions in column", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("Hello\x1B[3G"); // col 3 (1-based) -> col 2 (0-based)
    INFO("col 2 after CHA 3");
    REQUIRE(ts.host.col() == 2);
}

TEST_CASE("terminal: CSI E and F cursor next/preceding line", "[terminal]")
{
    TermSetup ts(20, 5);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[3;5H\x1B[2E"); // row 3,col 5 -> CNL 2 -> row 5 (max 4), col 0
    INFO("row 4 after CNL 2");
    REQUIRE(ts.host.row() == 4);
    INFO("col 0 after CNL");
    REQUIRE(ts.host.col() == 0);
    ts.host.feed("\x1B[3F"); // CPL 3 -> row 1, col 0
    INFO("row 1 after CPL 3");
    REQUIRE(ts.host.row() == 1);
    INFO("col 0 after CPL");
    REQUIRE(ts.host.col() == 0);
}

TEST_CASE("terminal: cursor save and restore", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[3;5H\x1B[s\x1B[1;1H\x1B[u");
    INFO("restored col 4 (0-based)");
    REQUIRE(ts.host.col() == 4);
    INFO("restored row 2 (0-based)");
    REQUIRE(ts.host.row() == 2);
}

TEST_CASE("terminal: backspace moves cursor left", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("ABC\b");
    INFO("col 2 after ABC+backspace");
    REQUIRE(ts.host.col() == 2);
}

TEST_CASE("terminal: tab advances to next 8-column boundary", "[terminal]")
{
    TermSetup ts(20, 3);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("AB\t");
    INFO("tab from col 2 to col 8");
    REQUIRE(ts.host.col() == 8);
    ts.host.feed("\t");
    INFO("second tab to col 16");
    REQUIRE(ts.host.col() == 16);
}

TEST_CASE("terminal: CSI d VPA sets row directly", "[terminal]")
{
    TermSetup ts(10, 5);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[3d");
    INFO("VPA row 2 (0-based)");
    REQUIRE(ts.host.row() == 2);
}

TEST_CASE("terminal: CSI K erases to end of line", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("Hello\x1B[3G\x1B[K"); // write, move to col 3 (1-based = col 2 0-based), erase to EOL
    INFO("col 0 preserved");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("H"));
    INFO("col 1 preserved");
    REQUIRE(ts.host.cell_text(1, 0) == std::string("e"));
    INFO("col 2 erased");
    REQUIRE(ts.host.cell_text(2, 0) == std::string(" "));
    INFO("col 3 erased");
    REQUIRE(ts.host.cell_text(3, 0) == std::string(" "));
    INFO("col 4 erased");
    REQUIRE(ts.host.cell_text(4, 0) == std::string(" "));
}

TEST_CASE("terminal: CSI 2K erases whole line", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("Hello\x1B[3G\x1B[2K");
    for (int col = 0; col < 5; ++col)
    {
        INFO("col erased");
        REQUIRE(ts.host.cell_text(col, 0) == std::string(" "));
    }
}

TEST_CASE("terminal: CSI 2J clears entire screen", "[terminal]")
{
    TermSetup ts(5, 3);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("ABCDE\r\nFGHIJ\r\nKLMNO\x1B[2J");
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 5; ++col)
        {
            INFO("cell cleared");
            REQUIRE(ts.host.cell_text(col, row) == std::string(" "));
        }
}

TEST_CASE("terminal: SGR colors produce distinct hl_attr_ids", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[31mA\x1B[32mB\x1B[0mC");
    INFO("A has non-default hl");
    REQUIRE(ts.host.cell_hl(0, 0) != 0);
    INFO("B has non-default hl");
    REQUIRE(ts.host.cell_hl(1, 0) != 0);
    INFO("A and B have different hl");
    REQUIRE(ts.host.cell_hl(0, 0) != ts.host.cell_hl(1, 0));
    INFO("C has default hl after reset");
    REQUIRE(ts.host.cell_hl(2, 0) == static_cast<uint16_t>(0));
}

TEST_CASE("terminal: DECSTBM scroll region restricts newline scroll", "[terminal]")
{
    TermSetup ts(5, 5);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD\r\nEEEEE");
    // Set scroll region rows 2-4 (1-based), cursor to bottom of region, then newline
    ts.host.feed("\x1B[2;4r\x1B[4;1H\n");

    INFO("row 0 unchanged");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("A"));
    INFO("row 1 has C after region scroll");
    REQUIRE(ts.host.cell_text(0, 1) == std::string("C"));
    INFO("row 2 has D");
    REQUIRE(ts.host.cell_text(0, 2) == std::string("D"));
    INFO("row 3 blank after scroll");
    REQUIRE(ts.host.cell_text(0, 3) == std::string(" "));
    INFO("row 4 unchanged");
    REQUIRE(ts.host.cell_text(0, 4) == std::string("E"));
}

TEST_CASE("terminal: CSI S scrolls up within scroll region", "[terminal]")
{
    TermSetup ts(5, 5);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD\r\nEEEEE");
    ts.host.feed("\x1B[2;4r\x1B[S"); // region rows 2-4, SU 1

    INFO("row 0 unchanged");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("A"));
    INFO("row 1 has C after SU");
    REQUIRE(ts.host.cell_text(0, 1) == std::string("C"));
    INFO("row 2 has D after SU");
    REQUIRE(ts.host.cell_text(0, 2) == std::string("D"));
    INFO("row 3 blank after SU");
    REQUIRE(ts.host.cell_text(0, 3) == std::string(" "));
    INFO("row 4 unchanged");
    REQUIRE(ts.host.cell_text(0, 4) == std::string("E"));
}

TEST_CASE("terminal: CSI T scrolls down within scroll region", "[terminal]")
{
    TermSetup ts(5, 5);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD\r\nEEEEE");
    ts.host.feed("\x1B[2;4r\x1B[T"); // region rows 2-4, SD 1

    INFO("row 0 unchanged");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("A"));
    INFO("row 1 blank after SD");
    REQUIRE(ts.host.cell_text(0, 1) == std::string(" "));
    INFO("row 2 has B after SD");
    REQUIRE(ts.host.cell_text(0, 2) == std::string("B"));
    INFO("row 3 has C after SD");
    REQUIRE(ts.host.cell_text(0, 3) == std::string("C"));
    INFO("row 4 unchanged");
    REQUIRE(ts.host.cell_text(0, 4) == std::string("E"));
}

TEST_CASE("terminal: CSI L inserts blank lines pushing content down", "[terminal]")
{
    TermSetup ts(5, 5);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD\r\nEEEEE");
    ts.host.feed("\x1B[2;1H\x1B[2L"); // cursor to row 2 col 1, insert 2 lines

    INFO("row 0 unchanged");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("A"));
    INFO("row 1 blank (inserted)");
    REQUIRE(ts.host.cell_text(0, 1) == std::string(" "));
    INFO("row 2 blank (inserted)");
    REQUIRE(ts.host.cell_text(0, 2) == std::string(" "));
    INFO("row 3 has B (pushed down)");
    REQUIRE(ts.host.cell_text(0, 3) == std::string("B"));
    INFO("row 4 has C (D/E fall off)");
    REQUIRE(ts.host.cell_text(0, 4) == std::string("C"));
}

TEST_CASE("terminal: CSI M deletes lines pulling content up", "[terminal]")
{
    TermSetup ts(5, 5);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD\r\nEEEEE");
    ts.host.feed("\x1B[2;1H\x1B[2M"); // cursor to row 2 col 1, delete 2 lines

    INFO("row 0 unchanged");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("A"));
    INFO("row 1 has D (pulled up)");
    REQUIRE(ts.host.cell_text(0, 1) == std::string("D"));
    INFO("row 2 has E (pulled up)");
    REQUIRE(ts.host.cell_text(0, 2) == std::string("E"));
    INFO("row 3 blank");
    REQUIRE(ts.host.cell_text(0, 3) == std::string(" "));
    INFO("row 4 blank");
    REQUIRE(ts.host.cell_text(0, 4) == std::string(" "));
}

TEST_CASE("terminal: CSI @ inserts blank characters shifting right", "[terminal]")
{
    TermSetup ts(6, 1);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    // Write ABCDE, then move to col 3 (1-based = col 2 0-based) via CHA, insert 2 chars
    ts.host.feed("ABCDE\x1B[3G\x1B[2@");

    INFO("col 0 unchanged");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("A"));
    INFO("col 1 unchanged");
    REQUIRE(ts.host.cell_text(1, 0) == std::string("B"));
    INFO("col 2 blank (inserted)");
    REQUIRE(ts.host.cell_text(2, 0) == std::string(" "));
    INFO("col 3 blank (inserted)");
    REQUIRE(ts.host.cell_text(3, 0) == std::string(" "));
    INFO("col 4 has C (shifted)");
    REQUIRE(ts.host.cell_text(4, 0) == std::string("C"));
    INFO("col 5 has D");
    REQUIRE(ts.host.cell_text(5, 0) == std::string("D"));
}

TEST_CASE("terminal: CSI P deletes characters pulling left", "[terminal]")
{
    TermSetup ts(6, 1);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    // Write ABCDEF, then move to col 3 (1-based = col 2 0-based) via CHA, delete 2 chars
    ts.host.feed("ABCDEF\x1B[3G\x1B[2P");

    INFO("col 0 unchanged");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("A"));
    INFO("col 1 unchanged");
    REQUIRE(ts.host.cell_text(1, 0) == std::string("B"));
    INFO("col 2 has E (pulled left)");
    REQUIRE(ts.host.cell_text(2, 0) == std::string("E"));
    INFO("col 3 has F");
    REQUIRE(ts.host.cell_text(3, 0) == std::string("F"));
    INFO("col 4 blank");
    REQUIRE(ts.host.cell_text(4, 0) == std::string(" "));
    INFO("col 5 blank");
    REQUIRE(ts.host.cell_text(5, 0) == std::string(" "));
}

TEST_CASE("terminal: CSI X erases characters in place", "[terminal]")
{
    TermSetup ts(6, 1);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    // Write ABCDEF, then move to col 3 (1-based = col 2 0-based) via CHA, erase 3 chars
    ts.host.feed("ABCDEF\x1B[3G\x1B[3X");

    INFO("col 0 unchanged");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("A"));
    INFO("col 1 unchanged");
    REQUIRE(ts.host.cell_text(1, 0) == std::string("B"));
    INFO("col 2 erased");
    REQUIRE(ts.host.cell_text(2, 0) == std::string(" "));
    INFO("col 3 erased");
    REQUIRE(ts.host.cell_text(3, 0) == std::string(" "));
    INFO("col 4 erased");
    REQUIRE(ts.host.cell_text(4, 0) == std::string(" "));
    INFO("col 5 unchanged");
    REQUIRE(ts.host.cell_text(5, 0) == std::string("F"));
}

TEST_CASE("terminal: alternate screen entry clears screen", "[terminal]")
{
    TermSetup ts(5, 3);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("Hello\x1B[?1049h");
    INFO("alt screen col 0 blank");
    REQUIRE(ts.host.cell_text(0, 0) == std::string(" "));
    INFO("alt screen col 1 blank");
    REQUIRE(ts.host.cell_text(1, 0) == std::string(" "));
}

TEST_CASE("terminal: alternate screen exit restores main screen content", "[terminal]")
{
    TermSetup ts(5, 3);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("Hello\x1B[?1049h"); // enter alt screen (saves main)
    ts.host.feed("World"); // write to alt screen
    ts.host.feed("\x1B[?1049l"); // leave alt screen (restores main)
    INFO("H restored");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("H"));
    INFO("e restored");
    REQUIRE(ts.host.cell_text(1, 0) == std::string("e"));
    INFO("o restored");
    REQUIRE(ts.host.cell_text(4, 0) == std::string("o"));
}

TEST_CASE("terminal: pending wrap wraps on next character", "[terminal]")
{
    TermSetup ts(5, 3);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("ABCDE"); // fills row 0
    ts.host.feed("F"); // wraps to row 1 col 0
    INFO("F wrapped to row 1");
    REQUIRE(ts.host.cell_text(0, 1) == std::string("F"));
    INFO("cursor on row 1");
    REQUIRE(ts.host.row() == 1);
    INFO("cursor at col 1 after F");
    REQUIRE(ts.host.col() == 1);
}

TEST_CASE("terminal: cursor movement clears pending wrap", "[terminal]")
{
    TermSetup ts(5, 3);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("ABCDE"); // fills row 0, pending wrap
    ts.host.feed("\x1B[G"); // CHA to col 1 (1-based) = col 0, clears pending wrap
    ts.host.feed("X"); // overwrites row 0 col 0, does NOT wrap
    INFO("X at row 0 col 0");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("X"));
    INFO("cursor stays on row 0");
    REQUIRE(ts.host.row() == 0);
}

TEST_CASE("terminal: auto-wrap off prevents wrap", "[terminal]")
{
    TermSetup ts(5, 3);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[?7l"); // disable auto-wrap
    ts.host.feed("ABCDEFG"); // 7 chars in 5-wide terminal
    // With no wrap, chars beyond col 4 overwrite col 4
    INFO("col 4 has G (last written)");
    REQUIRE(ts.host.cell_text(4, 0) == std::string("G"));
    INFO("cursor stays on row 0");
    REQUIRE(ts.host.row() == 0);
}

TEST_CASE("terminal: scrolled-off rows accumulate in scrollback", "[terminal]")
{
    TermSetup ts(5, 3);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    // Write 5 rows to a 3-row terminal — 2 rows scroll off
    ts.host.feed("AAA\r\nBBB\r\nCCC\r\nDDD\r\nEEE");
    // Live view should show CCC DDD EEE
    INFO("live row 0 is C");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("C"));
    INFO("live row 1 is D");
    REQUIRE(ts.host.cell_text(0, 1) == std::string("D"));
    INFO("live row 2 is E");
    REQUIRE(ts.host.cell_text(0, 2) == std::string("E"));
}

TEST_CASE("terminal: OSC title sequence parses without crash", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B]0;My Title\x07");
    INFO("OSC title sequence parsed without crash");
    REQUIRE(true);
}

TEST_CASE("terminal: SGR 0 full reset clears all attributes", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    // Set bold, italic, underline, strikethrough and a foreground color, then reset
    ts.host.feed("\x1B[1;3;4;9;31mA\x1B[0mB");
    INFO("A has non-default hl with multiple attributes");
    REQUIRE(ts.host.cell_hl(0, 0) != 0);
    INFO("B has default hl after SGR 0");
    REQUIRE(ts.host.cell_hl(1, 0) == static_cast<uint16_t>(0));
}

TEST_CASE("terminal: SGR 39 resets foreground to default", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    // Set foreground red, then write A; then reset foreground with SGR 39 and write B
    ts.host.feed("\x1B[31mA\x1B[39mB");
    const uint16_t hl_a = ts.host.cell_hl(0, 0);
    const uint16_t hl_b = ts.host.cell_hl(1, 0);
    INFO("A has non-default hl with red fg");
    REQUIRE(hl_a != 0);
    // After SGR 39, the fg is cleared; if no other attrs remain, hl_id returns to 0
    INFO("A and B have different hl_ids (fg reset changes attributes)");
    REQUIRE(hl_a != hl_b);
    // B should have default hl (no fg set)
    INFO("B has default hl after SGR 39 resets fg");
    REQUIRE(hl_b == static_cast<uint16_t>(0));
}

TEST_CASE("terminal: SGR 49 resets background to default", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[41mA\x1B[49mB");
    const uint16_t hl_a = ts.host.cell_hl(0, 0);
    const uint16_t hl_b = ts.host.cell_hl(1, 0);
    INFO("A has non-default hl with red bg");
    REQUIRE(hl_a != 0);
    INFO("A and B have different hl_ids");
    REQUIRE(hl_a != hl_b);
    INFO("B has default hl after SGR 49 resets bg");
    REQUIRE(hl_b == static_cast<uint16_t>(0));
}

TEST_CASE("terminal: SGR 1 bold and SGR 22 bold-off", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[1mA\x1B[22mB");
    const uint16_t hl_a = ts.host.cell_hl(0, 0);
    const uint16_t hl_b = ts.host.cell_hl(1, 0);
    INFO("A has bold hl");
    REQUIRE(hl_a != 0);
    INFO("B has default hl after SGR 22");
    REQUIRE(hl_b == static_cast<uint16_t>(0));
}

TEST_CASE("terminal: SGR 3 italic and SGR 23 italic-off", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[3mA\x1B[23mB");
    const uint16_t hl_a = ts.host.cell_hl(0, 0);
    const uint16_t hl_b = ts.host.cell_hl(1, 0);
    INFO("A has italic hl");
    REQUIRE(hl_a != 0);
    INFO("B has default hl after SGR 23");
    REQUIRE(hl_b == static_cast<uint16_t>(0));
}

TEST_CASE("terminal: SGR 4 underline and SGR 24 underline-off", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[4mA\x1B[24mB");
    const uint16_t hl_a = ts.host.cell_hl(0, 0);
    const uint16_t hl_b = ts.host.cell_hl(1, 0);
    INFO("A has underline hl");
    REQUIRE(hl_a != 0);
    INFO("B has default hl after SGR 24");
    REQUIRE(hl_b == static_cast<uint16_t>(0));
}

TEST_CASE("terminal: SGR 9 strikethrough and SGR 29 strikethrough-off", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[9mA\x1B[29mB");
    const uint16_t hl_a = ts.host.cell_hl(0, 0);
    const uint16_t hl_b = ts.host.cell_hl(1, 0);
    INFO("A has strikethrough hl");
    REQUIRE(hl_a != 0);
    INFO("B has default hl after SGR 29");
    REQUIRE(hl_b == static_cast<uint16_t>(0));
}

TEST_CASE("terminal: SGR 30-37 set foreground colors producing distinct hl ids", "[terminal]")
{
    TermSetup ts(20, 2);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    // Write one character per ANSI foreground color (30–37)
    for (int i = 30; i <= 37; ++i)
    {
        char seq[16];
        snprintf(seq, sizeof(seq), "\x1B[%dmX", i);
        ts.host.feed(seq);
    }
    // All 8 characters should have non-zero highlight ids
    for (int col = 0; col < 8; ++col)
    {
        INFO("SGR 30-37 characters have non-default hl");
        REQUIRE(ts.host.cell_hl(col, 0) != 0);
    }
    // Adjacent colors should differ
    for (int col = 0; col < 7; ++col)
    {
        INFO("adjacent ANSI foreground colors have distinct hl ids");
        REQUIRE(ts.host.cell_hl(col, 0) != ts.host.cell_hl(col + 1, 0));
    }
}

TEST_CASE("terminal: SGR 40-47 set background colors producing distinct hl ids", "[terminal]")
{
    TermSetup ts(20, 2);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    for (int i = 40; i <= 47; ++i)
    {
        char seq[16];
        snprintf(seq, sizeof(seq), "\x1B[%dmX", i);
        ts.host.feed(seq);
    }
    for (int col = 0; col < 8; ++col)
    {
        INFO("SGR 40-47 characters have non-default hl");
        REQUIRE(ts.host.cell_hl(col, 0) != 0);
    }
    for (int col = 0; col < 7; ++col)
    {
        INFO("adjacent ANSI background colors have distinct hl ids");
        REQUIRE(ts.host.cell_hl(col, 0) != ts.host.cell_hl(col + 1, 0));
    }
}

TEST_CASE("terminal: SGR 90-97 set bright foreground colors", "[terminal]")
{
    TermSetup ts(20, 2);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    for (int i = 90; i <= 97; ++i)
    {
        char seq[16];
        snprintf(seq, sizeof(seq), "\x1B[%dmX", i);
        ts.host.feed(seq);
    }
    for (int col = 0; col < 8; ++col)
    {
        INFO("SGR 90-97 characters have non-default hl");
        REQUIRE(ts.host.cell_hl(col, 0) != 0);
    }
    // Bright colors (90-97) differ from normal colors (30-37)
    for (int col = 0; col < 7; ++col)
    {
        INFO("adjacent bright foreground colors have distinct hl ids");
        REQUIRE(ts.host.cell_hl(col, 0) != ts.host.cell_hl(col + 1, 0));
    }
}

TEST_CASE("terminal: SGR 100-107 set bright background colors", "[terminal]")
{
    TermSetup ts(20, 2);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    for (int i = 100; i <= 107; ++i)
    {
        char seq[16];
        snprintf(seq, sizeof(seq), "\x1B[%dmX", i);
        ts.host.feed(seq);
    }
    for (int col = 0; col < 8; ++col)
    {
        INFO("SGR 100-107 characters have non-default hl");
        REQUIRE(ts.host.cell_hl(col, 0) != 0);
    }
    for (int col = 0; col < 7; ++col)
    {
        INFO("adjacent bright background colors have distinct hl ids");
        REQUIRE(ts.host.cell_hl(col, 0) != ts.host.cell_hl(col + 1, 0));
    }
}

TEST_CASE("terminal: SGR 38;2 true-color foreground sets distinct hl", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    // True-color red: SGR 38;2;255;0;0
    ts.host.feed("\x1B[38;2;255;0;0mA");
    // True-color blue: SGR 38;2;0;0;255
    ts.host.feed("\x1B[38;2;0;0;255mB");
    ts.host.feed("\x1B[0mC");
    INFO("A has true-color fg hl");
    REQUIRE(ts.host.cell_hl(0, 0) != 0);
    INFO("B has true-color fg hl");
    REQUIRE(ts.host.cell_hl(1, 0) != 0);
    INFO("red and blue fg produce distinct hl ids");
    REQUIRE(ts.host.cell_hl(0, 0) != ts.host.cell_hl(1, 0));
    INFO("C has default hl after SGR 0");
    REQUIRE(ts.host.cell_hl(2, 0) == static_cast<uint16_t>(0));
}

TEST_CASE("terminal: SGR 48;2 true-color background sets distinct hl", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[48;2;0;255;0mA");
    ts.host.feed("\x1B[48;2;255;0;255mB");
    ts.host.feed("\x1B[0mC");
    INFO("A has true-color bg hl");
    REQUIRE(ts.host.cell_hl(0, 0) != 0);
    INFO("B has true-color bg hl");
    REQUIRE(ts.host.cell_hl(1, 0) != 0);
    INFO("green and magenta bg produce distinct hl ids");
    REQUIRE(ts.host.cell_hl(0, 0) != ts.host.cell_hl(1, 0));
    INFO("C has default hl after SGR 0");
    REQUIRE(ts.host.cell_hl(2, 0) == static_cast<uint16_t>(0));
}

TEST_CASE("terminal: SGR 38;5 256-color foreground sets distinct hl", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    // 256-color index 200 and 201 should produce different colors
    ts.host.feed("\x1B[38;5;200mA");
    ts.host.feed("\x1B[38;5;201mB");
    ts.host.feed("\x1B[0mC");
    INFO("A has 256-color fg hl");
    REQUIRE(ts.host.cell_hl(0, 0) != 0);
    INFO("B has 256-color fg hl");
    REQUIRE(ts.host.cell_hl(1, 0) != 0);
    INFO("256-color index 200 and 201 produce distinct hl ids");
    REQUIRE(ts.host.cell_hl(0, 0) != ts.host.cell_hl(1, 0));
    INFO("C has default hl after SGR 0");
    REQUIRE(ts.host.cell_hl(2, 0) == static_cast<uint16_t>(0));
}

TEST_CASE("terminal: SGR 48;5 256-color background sets distinct hl", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[48;5;100mA");
    ts.host.feed("\x1B[48;5;101mB");
    ts.host.feed("\x1B[0mC");
    INFO("A has 256-color bg hl");
    REQUIRE(ts.host.cell_hl(0, 0) != 0);
    INFO("B has 256-color bg hl");
    REQUIRE(ts.host.cell_hl(1, 0) != 0);
    INFO("256-color index 100 and 101 produce distinct hl ids");
    REQUIRE(ts.host.cell_hl(0, 0) != ts.host.cell_hl(1, 0));
    INFO("C has default hl after SGR 0");
    REQUIRE(ts.host.cell_hl(2, 0) == static_cast<uint16_t>(0));
}

TEST_CASE("terminal: SGR 38;5;0 uses ANSI black (index 0)", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[38;5;0mA\x1B[0mB");
    INFO("A has 256-color palette index 0 hl");
    REQUIRE(ts.host.cell_hl(0, 0) != 0);
    INFO("B default after reset");
    REQUIRE(ts.host.cell_hl(1, 0) == static_cast<uint16_t>(0));
}

TEST_CASE("terminal: SGR combinations produce the correct cumulative effect", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    // Bold + italic together
    ts.host.feed("\x1B[1;3mA");
    // Bold only
    ts.host.feed("\x1B[23mB"); // turn off italic
    // Default
    ts.host.feed("\x1B[22mC"); // turn off bold
    const uint16_t hl_a = ts.host.cell_hl(0, 0);
    const uint16_t hl_b = ts.host.cell_hl(1, 0);
    const uint16_t hl_c = ts.host.cell_hl(2, 0);
    INFO("A has bold+italic hl");
    REQUIRE(hl_a != 0);
    INFO("B has bold hl");
    REQUIRE(hl_b != 0);
    INFO("C has default hl after turning off bold");
    REQUIRE(hl_c == static_cast<uint16_t>(0));
    INFO("bold+italic differs from bold-only");
    REQUIRE(hl_a != hl_b);
}

TEST_CASE("terminal: alt-screen enter preserves main cursor position", "[terminal]")
{
    TermSetup ts(10, 5);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[3;5H"); // move to row 3, col 5 (1-based) = row 2, col 4 (0-based)
    ts.host.feed("X"); // write at position
    ts.host.feed("\x1B[?1049h"); // enter alt screen
    // Alt screen starts at (0,0)
    INFO("cursor resets to col 0 on alt screen");
    REQUIRE(ts.host.col() == 0);
    INFO("cursor resets to row 0 on alt screen");
    REQUIRE(ts.host.row() == 0);
    // Alt screen should be blank
    INFO("alt screen row 0 col 0 blank");
    REQUIRE(ts.host.cell_text(0, 0) == std::string(" "));
    ts.host.feed("\x1B[?1049l"); // exit alt screen
    // Main screen content restored
    INFO("X restored at correct position after alt-screen exit");
    REQUIRE(ts.host.cell_text(4, 2) == std::string("X"));
}

TEST_CASE("terminal: alt-screen double enter is idempotent", "[terminal]")
{
    TermSetup ts(5, 3);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("Hello");
    ts.host.feed("\x1B[?1049h"); // first enter
    ts.host.feed("ALT");
    ts.host.feed("\x1B[?1049h"); // second enter (should be no-op or safe)
    ts.host.feed("\x1B[?1049l"); // exit
    // Main screen should be restored
    INFO("H restored after double-enter exit");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("H"));
}

TEST_CASE("terminal: origin mode restricts cursor to scroll region on positioning", "[terminal]")
{
    TermSetup ts(10, 6);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    // Set scroll region rows 3-5 (1-based), then enable origin mode
    ts.host.feed("\x1B[3;5r"); // scroll region rows 3-5 (1-based) = rows 2-4 (0-based)
    ts.host.feed("\x1B[?6h"); // enable origin mode
    // In origin mode, CUP (H) is relative to scroll region top
    ts.host.feed("\x1B[1;1H"); // should go to scroll-region top (row 2 in absolute terms)
    // With origin mode on, cursor row 0 = scroll_top (row 2 absolute)
    // We just verify no crash and that row is within the scroll region
    INFO("origin mode cursor row is within scroll region");
    REQUIRE(ts.host.row() >= 2);
    INFO("origin mode cursor row is within scroll region (upper bound)");
    REQUIRE(ts.host.row() <= 4);
    ts.host.feed("\x1B[?6l"); // disable origin mode
    ts.host.feed("\x1B[r"); // reset scroll region
}

TEST_CASE("terminal: nvim command-mode layout: statusline and cmdline at bottom rows", "[terminal]")
{
    // Simulate the sequence nvim uses when showing a statusline + cmdline:
    // - enter alt screen
    // - set scroll region to protect last 2 rows
    // - position cursor to write statusline at row N-2 (0-indexed)
    // - position cursor to write cmdline at row N-1 (0-indexed)
    // Verifies that CUP positioning with DECSTBM lands content at the correct rows.
    const int cols = 20;
    const int rows = 10;
    TermSetup ts(cols, rows);
    INFO("host must initialize");
    REQUIRE(ts.ok);

    // Enter alt screen (nvim uses this)
    ts.host.feed("\x1B[?1049h");

    // Nvim sets scroll region to protect last 2 rows (statusline + cmdline):
    // ESC[1;8r = rows 1..8 (1-indexed) = rows 0..7 (0-indexed) as scroll region
    // Rows 8 and 9 (0-indexed) = rows 9 and 10 (1-indexed) are protected
    ts.host.feed("\x1B[1;8r");

    // Verify cursor reset to home after DECSTBM
    INFO("cursor reset to row 0 after DECSTBM");
    REQUIRE(ts.host.row() == 0);

    // Fill editing area with some content
    ts.host.feed("\x1B[1;1H"); // row 0, col 0
    ts.host.feed("EDITING");

    // Position cursor to statusline row (row 9, 1-indexed = row 8, 0-indexed)
    ts.host.feed("\x1B[9;1H");
    INFO("cursor at statusline row (0-indexed 8) after ESC[9;1H");
    REQUIRE(ts.host.row() == 8);
    ts.host.feed("STATUSLINE");
    INFO("statusline content at row 8");
    REQUIRE(ts.host.cell_text(0, 8) == std::string("S"));
    REQUIRE(ts.host.cell_text(9, 8) == std::string("E")); // "STATUSLINE"[9] = 'E'

    // Position cursor to cmdline row (row 10, 1-indexed = row 9, 0-indexed)
    ts.host.feed("\x1B[10;1H");
    INFO("cursor at cmdline row (0-indexed 9) after ESC[10;1H");
    REQUIRE(ts.host.row() == 9);
    ts.host.feed(":command");
    INFO("cmdline content at row 9");
    REQUIRE(ts.host.cell_text(0, 9) == std::string(":"));
    REQUIRE(ts.host.cell_text(1, 9) == std::string("c"));

    // Verify editing area content is at the right row
    INFO("editing area content at row 0");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("E"));

    // Verify no content at wrong rows (statusline should NOT be at row 9 or row 0)
    INFO("statusline NOT at cmdline row");
    REQUIRE(ts.host.cell_text(0, 9) != std::string("S"));
}

TEST_CASE("terminal: auto-wrap mode can be toggled with ?7h and ?7l", "[terminal]")
{
    TermSetup ts(5, 3);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    // Disable wrap, overfill row 0, then re-enable and overfill again
    ts.host.feed("\x1B[?7l"); // disable wrap
    ts.host.feed("ABCDEFG"); // 7 chars; last 2 overwrite col 4
    INFO("no-wrap: last char overwrites at last col");
    REQUIRE(ts.host.cell_text(4, 0) == std::string("G"));
    INFO("no-wrap: cursor stays on row 0");
    REQUIRE(ts.host.row() == 0);
    ts.host.feed("\x1B[?7h"); // re-enable wrap
    ts.host.feed("\x1B[2;1H");
    ts.host.feed("ABCDEF"); // 6 chars in 5-wide terminal: wraps to row 2
    INFO("wrap enabled: F on row 2");
    REQUIRE(ts.host.cell_text(0, 2) == std::string("F"));
}

// ---------------------------------------------------------------------------
// OSC 7 — working directory notification
// ---------------------------------------------------------------------------

TEST_CASE("terminal: OSC 7 sets window title to directory basename", "[terminal]")
{
    TermSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    // Standard OSC 7 with localhost hostname, BEL terminator
    ts.host.feed("\x1B]7;file://localhost/Users/chris/projects\x07");
    REQUIRE(ts.callbacks.last_window_title == "projects");
}

TEST_CASE("terminal: OSC 7 with empty hostname", "[terminal]")
{
    TermSetup ts;
    REQUIRE(ts.ok);
    // Empty hostname (common on macOS zsh)
    ts.host.feed("\x1B]7;file:///tmp\x07");
    REQUIRE(ts.callbacks.last_window_title == "tmp");
}

TEST_CASE("terminal: OSC 7 with trailing slash", "[terminal]")
{
    TermSetup ts;
    REQUIRE(ts.ok);
    ts.host.feed("\x1B]7;file://host/home/user/\x07");
    REQUIRE(ts.callbacks.last_window_title == "user");
}

TEST_CASE("terminal: OSC 7 root directory", "[terminal]")
{
    TermSetup ts;
    REQUIRE(ts.ok);
    ts.host.feed("\x1B]7;file://localhost/\x07");
    REQUIRE(ts.callbacks.last_window_title == "/");
}

TEST_CASE("terminal: OSC 7 percent-decodes path", "[terminal]")
{
    TermSetup ts;
    REQUIRE(ts.ok);
    // %20 = space, %2F should not appear in practice but test mixed encoding
    ts.host.feed("\x1B]7;file://localhost/home/my%20folder\x07");
    REQUIRE(ts.callbacks.last_window_title == "my folder");
}

TEST_CASE("terminal: OSC 7 with ST terminator", "[terminal]")
{
    TermSetup ts;
    REQUIRE(ts.ok);
    // ST = ESC backslash
    ts.host.feed("\x1B]7;file:///var/log\x1B\\");
    REQUIRE(ts.callbacks.last_window_title == "log");
}

TEST_CASE("terminal: OSC 7 malformed URI is ignored", "[terminal]")
{
    TermSetup ts;
    REQUIRE(ts.ok);
    // Not a file:// URI — should be silently ignored
    ts.host.feed("\x1B]7;http://example.com/path\x07");
    REQUIRE(ts.callbacks.last_window_title.empty());
}

TEST_CASE("terminal: OSC 0 title still works alongside OSC 7", "[terminal]")
{
    TermSetup ts;
    REQUIRE(ts.ok);
    // OSC 0 sets title
    ts.host.feed("\x1B]0;My Terminal\x07");
    REQUIRE(ts.callbacks.last_window_title == "My Terminal");
    // OSC 7 overrides it
    ts.host.feed("\x1B]7;file:///home/user/code\x07");
    REQUIRE(ts.callbacks.last_window_title == "code");
}

TEST_CASE("terminal: OSC 7 non-URI string produces WARN and is ignored", "[terminal]")
{
    ScopedLogCapture logs(LogLevel::Warn);
    TermSetup ts;
    REQUIRE(ts.ok);
    ts.host.feed("\x1B]7;not-a-url\x07");
    REQUIRE(ts.callbacks.last_window_title.empty());
    bool found_warn = false;
    for (const auto& rec : logs.records)
    {
        if (rec.level == LogLevel::Warn && rec.message.find("malformed URI") != std::string::npos)
        {
            found_warn = true;
            break;
        }
    }
    REQUIRE(found_warn);
}

TEST_CASE("terminal: OSC 7 empty URI produces WARN and is ignored", "[terminal]")
{
    ScopedLogCapture logs(LogLevel::Warn);
    TermSetup ts;
    REQUIRE(ts.ok);
    ts.host.feed("\x1B]7;\x07");
    REQUIRE(ts.callbacks.last_window_title.empty());
    bool found_warn = false;
    for (const auto& rec : logs.records)
    {
        if (rec.level == LogLevel::Warn && rec.message.find("empty URI") != std::string::npos)
        {
            found_warn = true;
            break;
        }
    }
    REQUIRE(found_warn);
}

TEST_CASE("terminal: OSC 7 remote host URI updates CWD correctly", "[terminal]")
{
    TermSetup ts;
    REQUIRE(ts.ok);
    ts.host.feed("\x1B]7;file://remotehost/home/user/work\x07");
    REQUIRE(ts.callbacks.last_window_title == "work");
}

// ---------------------------------------------------------------------------
// DECSTBM (CSI r) scroll region bounds tests
// ---------------------------------------------------------------------------

TEST_CASE("terminal: DECSTBM top > bot is ignored", "[terminal]")
{
    TermSetup ts(10, 5);
    REQUIRE(ts.ok);

    INFO("default scroll_top is 0");
    REQUIRE(ts.host.scroll_top() == 0);
    INFO("default scroll_bottom is rows-1");
    REQUIRE(ts.host.scroll_bottom() == 4);

    // Set a valid region first
    ts.host.feed("\x1B[2;4r");
    INFO("valid region: scroll_top = 1");
    REQUIRE(ts.host.scroll_top() == 1);
    INFO("valid region: scroll_bottom = 3");
    REQUIRE(ts.host.scroll_bottom() == 3);

    // Invalid: top > bot
    ts.host.feed("\x1B[5;3r");
    INFO("invalid region ignored: scroll_top unchanged");
    REQUIRE(ts.host.scroll_top() == 1);
    INFO("invalid region ignored: scroll_bottom unchanged");
    REQUIRE(ts.host.scroll_bottom() == 3);
}

TEST_CASE("terminal: DECSTBM bot > rows is clamped", "[terminal]")
{
    TermSetup ts(10, 5);
    REQUIRE(ts.ok);

    ts.host.feed("\x1B[1;999r");
    INFO("scroll_top = 0 (param 1 -> 0-based 0)");
    REQUIRE(ts.host.scroll_top() == 0);
    INFO("scroll_bottom clamped to rows-1 = 4");
    REQUIRE(ts.host.scroll_bottom() == 4);
}

TEST_CASE("terminal: DECSTBM 0;0 resets to full screen", "[terminal]")
{
    TermSetup ts(10, 5);
    REQUIRE(ts.ok);

    ts.host.feed("\x1B[2;4r");
    INFO("restricted region: scroll_top = 1");
    REQUIRE(ts.host.scroll_top() == 1);
    INFO("restricted region: scroll_bottom = 3");
    REQUIRE(ts.host.scroll_bottom() == 3);

    ts.host.feed("\x1B[0;0r");
    INFO("reset: scroll_top = 0");
    REQUIRE(ts.host.scroll_top() == 0);
    INFO("reset: scroll_bottom = rows-1 = 4");
    REQUIRE(ts.host.scroll_bottom() == 4);
}
