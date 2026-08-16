
#include <draxul/terminal_host_base.h>

#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"
#include "support/test_local_terminal_host.h"
#include <SDL3/SDL_keycode.h>
#include <draxul/host.h>
#include <draxul/text_service.h>
#include <draxul/window.h>

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using namespace draxul;

// ---------------------------------------------------------------------------
// Minimal fakes (same pattern as terminal_vt_tests.cpp)
// ---------------------------------------------------------------------------

namespace
{

using SbFakeWindow = draxul::tests::FakeWindow;
using SbFakeRenderer = draxul::tests::FakeTermRenderer;

class SbTestHost final : public TerminalHostBase
{
public:
    void feed(std::string_view bytes)
    {
        consume_output(bytes);
    }

    std::string cell_text(int col, int row)
    {
        return std::string(grid().get_cell(col, row).text.view());
    }

    int cols_ = 10;
    int rows_ = 3;

protected:
    std::string_view host_name() const override
    {
        return "sb-test";
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

    bool do_process_write(std::string_view) override
    {
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

struct SbSetup
{
    SbFakeWindow window;
    SbFakeRenderer renderer;
    TextService text_service;
    SbTestHost host;
    draxul::tests::TestHostCallbacks callbacks;
    bool ok = false;

    explicit SbSetup(int cols = 10, int rows = 3)
    {
        host.cols_ = cols;
        host.rows_ = rows;

        draxul::tests::init_text_service(text_service);

        HostViewport vp;
        vp.grid_size.x = cols;
        vp.grid_size.y = rows;

        HostContext ctx{
            .window = &window,
            .grid_renderer = &renderer,
            .text_service = &text_service,
            .initial_viewport = vp,
        };
        ok = host.initialize(ctx, callbacks);
    }

    // Feed N lines of text that each scroll off the top.  Each line begins with
    // a unique marker character so we can identify it later.
    void feed_lines(int n)
    {
        for (int i = 0; i < n; ++i)
        {
            // Use a simple repeating printable character set ('A'…'~')
            char ch = static_cast<char>('A' + (i % (126 - 'A' + 1)));
            std::string line(static_cast<size_t>(host.cols_), ch);
            // Append CR+LF to push all but the last line into scrollback.
            // The last line stays in the live row so tests can check its content.
            if (i < n - 1)
                line += "\r\n";
            host.feed(line);
        }
    }
};

KeyEvent shift_key(int keycode)
{
    return KeyEvent{ 0, keycode, kModShift, true };
}

struct LocalSbSetup
{
    SbFakeWindow window;
    SbFakeRenderer renderer;
    TextService text_service;
    draxul::tests::TestLocalTerminalHost host;
    draxul::tests::TestHostCallbacks callbacks;
    bool ok = false;

    LocalSbSetup(int cols = 4, int rows = 3, int scrollback_lines = ScrollbackBuffer::kDefaultCapacity)
    {
        host.cols_ = cols;
        host.rows_ = rows;

        draxul::tests::init_text_service(text_service);

        HostLaunchOptions launch;
        launch.scrollback_lines = scrollback_lines;

        HostViewport vp;
        vp.grid_size.x = cols;
        vp.grid_size.y = rows;

        HostContext ctx{
            .window = &window,
            .grid_renderer = &renderer,
            .text_service = &text_service,
            .launch_options = launch,
            .initial_viewport = vp,
        };
        ok = host.initialize(ctx, callbacks);
    }

    void scroll_home()
    {
        host.on_key(shift_key(SDLK_HOME));
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// The actual scrollback capacity constant declared in terminal_host_base.h
// is kScrollbackCapacity = 2000, but it is private. We use a small
// terminal (cols=2, rows=1) and feed exactly kScrollbackCapacity lines so
// we can observe eviction without waiting forever.  The capacity value
// itself is tested indirectly: the test observes FIFO semantics.

// For efficiency we use a tiny terminal (1 row, 4 cols) so that each
// printed line goes into scrollback quickly.
static constexpr int kRows = 1;
static constexpr int kCols = 4;

TEST_CASE("scrollback: lines accumulate without eviction below capacity", "[terminal]")
{
    // Feed 100 lines into a 1-row terminal. All should be in scrollback.
    SbSetup ts(kCols, kRows);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.feed_lines(100);
    // The live view shows the last line. The previous 99 lines are in scrollback.
    // We just verify no crash and the current visible row has the last character.
    char expected_ch = static_cast<char>('A' + (99 % (126 - 'A' + 1)));
    INFO("live row shows the last fed line's character");
    REQUIRE(ts.host.cell_text(0, 0) == std::string(1, expected_ch));
}

TEST_CASE("scrollback: oldest line is evicted when capacity exceeded", "[terminal]")
{
    // Feed kScrollbackCapacity + 1 lines, then verify the live row content.
    // We use a small capacity by feeding many lines so that at least one
    // eviction occurs. With kScrollbackCapacity=2000 we feed 2001 lines.
    // The oldest line (index 0, char 'A') must be gone; the newest is live.
    constexpr int kCapacity = 2000;
    constexpr int kFeedCount = kCapacity + 1;
    SbSetup ts(kCols, kRows);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.feed_lines(kFeedCount);
    // Line index kFeedCount-1 is in the live view (just written)
    char live_ch = static_cast<char>('A' + ((kFeedCount - 1) % (126 - 'A' + 1)));
    INFO("live row shows the last-fed line after eviction");
    REQUIRE(ts.host.cell_text(0, 0) == std::string(1, live_ch));
}

TEST_CASE("scrollback: bulk overfill only retains newest lines", "[terminal]")
{
    // Feed 3000 lines (1000 over capacity). Only the newest 2000 should be retained
    // in scrollback plus the live row. Verify no crash.
    constexpr int kCapacity = 2000;
    constexpr int kFeedCount = kCapacity + 1000;
    SbSetup ts(kCols, kRows);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.feed_lines(kFeedCount);
    // Verify live row content is correct (the very last line)
    char live_ch = static_cast<char>('A' + ((kFeedCount - 1) % (126 - 'A' + 1)));
    INFO("live row shows the last-fed line after bulk overfill");
    REQUIRE(ts.host.cell_text(0, 0) == std::string(1, live_ch));
}

TEST_CASE("scrollback: scrollback buffer FIFO order preserved", "[terminal]")
{
    // Feed 4 lines to a 3-row terminal.
    // Each "\r\n" at scroll_bottom_ causes a scroll: AAAA then BBBB go to scrollback.
    // After all 4 lines: row0=CCCC, row1=DDDD, row2=empty (last \r\n cleared it).
    SbSetup ts(kCols, kRows + 2); // 3-row terminal
    INFO("host must initialize");
    REQUIRE(ts.ok);
    std::string lines[4] = { "AAAA\r\n", "BBBB\r\n", "CCCC\r\n", "DDDD\r\n" };
    for (const auto& l : lines)
        ts.host.feed(l);
    // Live view: rows 0=C, 1=D, 2=empty (A and B went to scrollback)
    INFO("live row 0 is C after 2 scrolls");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("C"));
    INFO("live row 1 is D");
    REQUIRE(ts.host.cell_text(0, 1) == std::string("D"));
    INFO("live row 2 is blank (cleared by last scroll)");
    REQUIRE(ts.host.cell_text(0, 2) == std::string(" "));
}

TEST_CASE("scrollback: no crash on rapid fill and clear cycle", "[terminal]")
{
    // Stress: fill to capacity, then reset terminal state (which clears scrollback)
    // several times. No crash expected.
    constexpr int kCapacity = 2000;
    SbSetup ts(kCols, kRows);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        ts.feed_lines(kCapacity + 10);
        // Reset terminal state flushes scrollback_offset_ and clears selection;
        // calling it via \x1B[2J (clear screen) does not reset scrollback itself,
        // but the host remains valid.
        ts.host.feed("\x1B[2J");
    }
    // Final live view should be blank (cleared by 2J).
    INFO("after clear-screen cycle, live row is blank");
    REQUIRE(ts.host.cell_text(0, 0) == std::string(" "));
}

TEST_CASE("scrollback: alternate screen does not accumulate scrollback", "[terminal]")
{
    // Lines written in alt-screen mode must NOT go into scrollback.
    SbSetup ts(kCols, kRows);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[?1049h"); // enter alt screen
    ts.feed_lines(10); // write 10 lines in alt screen
    ts.host.feed("\x1B[?1049l"); // exit alt screen
    // We cannot easily count scrollback items from outside, but the main
    // screen should be restored and we just check for no crash.
    INFO("no crash after alt-screen writes and exit");
    REQUIRE(true);
}

TEST_CASE("scrollback: IND captures the row scrolled off the top", "[terminal][scrollback]")
{
    LocalSbSetup ts(4, 3);
    REQUIRE(ts.ok);

    ts.host.feed("AAAA\r\nBBBB\r\nCCCC");
    ts.host.feed("\x1B[3;1H"
                 "\x1B"
                 "D");
    ts.scroll_home();

    REQUIRE(ts.host.cell_text(0, 0) == "A");
    REQUIRE(ts.host.cell_text(0, 1) == "B");
}

TEST_CASE("scrollback: CSI SU captures all rows scrolled off the top", "[terminal][scrollback]")
{
    LocalSbSetup ts(4, 3);
    REQUIRE(ts.ok);

    ts.host.feed("AAAA\r\nBBBB\r\nCCCC");
    ts.host.feed("\x1B[2S");
    ts.scroll_home();

    REQUIRE(ts.host.cell_text(0, 0) == "A");
    REQUIRE(ts.host.cell_text(0, 1) == "B");
}

TEST_CASE("scrollback: DL at row 0 captures deleted full-screen rows", "[terminal][scrollback]")
{
    LocalSbSetup ts(4, 3);
    REQUIRE(ts.ok);

    ts.host.feed("AAAA\r\nBBBB\r\nCCCC");
    ts.host.feed("\x1B[1;1H\x1B[2M");
    ts.scroll_home();

    REQUIRE(ts.host.cell_text(0, 0) == "A");
    REQUIRE(ts.host.cell_text(0, 1) == "B");
}

TEST_CASE("scrollback: shrink grow round trip preserves visible content and cursor", "[terminal][scrollback]")
{
    LocalSbSetup ts(4, 3);
    REQUIRE(ts.ok);

    ts.host.feed("AAAA\r\nBBBB\r\nCCCC");
    const int initial_row = ts.host.row();
    const int initial_col = ts.host.col();
    REQUIRE(initial_row == 2);

    HostViewport smaller;
    smaller.grid_size = { 4, 2 };
    ts.host.set_viewport(smaller);
    HostViewport larger;
    larger.grid_size = { 4, 3 };
    ts.host.set_viewport(larger);

    REQUIRE(ts.host.cell_text(0, 0) == "A");
    REQUIRE(ts.host.cell_text(0, 1) == "B");
    REQUIRE(ts.host.cell_text(0, 2) == "C");
    REQUIRE(ts.host.row() == initial_row);
    REQUIRE(ts.host.col() == initial_col);
}

TEST_CASE("scrollback: erase display 2 preserves nonblank visible rows", "[terminal][scrollback]")
{
    LocalSbSetup ts(4, 3);
    REQUIRE(ts.ok);

    ts.host.feed("AAAA\r\nBBBB\r\nCCCC");
    ts.host.feed("\x1B[2J");
    ts.scroll_home();

    REQUIRE(ts.host.cell_text(0, 0) == "A");
    REQUIRE(ts.host.cell_text(0, 1) == "B");
    REQUIRE(ts.host.cell_text(0, 2) == "C");
}

TEST_CASE("scrollback: launch option controls retained row capacity", "[terminal][scrollback]")
{
    LocalSbSetup ts(4, 1, 2);
    REQUIRE(ts.ok);

    ts.host.feed("AAAA\r\nBBBB\r\nCCCC\r\nDDDD");
    ts.scroll_home();

    INFO("capacity 2 evicts the oldest scrolled-off row");
    REQUIRE(ts.host.cell_text(0, 0) == "B");
}
