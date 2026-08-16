
// Internal draxul-host header — added to test include path in CMakeLists.txt.
#include <draxul/terminal_host_base.h>

#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"
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
// Minimal fakes (mirrors terminal_vt_tests.cpp setup)
// ---------------------------------------------------------------------------

namespace
{

using PsFakeWindow = draxul::tests::FakeWindow;
using PsFakeRenderer = draxul::tests::FakeTermRenderer;

// ---------------------------------------------------------------------------
// TestTerminalHost — wraps TerminalHostBase for unit tests.
// ---------------------------------------------------------------------------

class PsTestHost final : public TerminalHostBase
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

    int col() const
    {
        return vt_state().col;
    }
    int row() const
    {
        return vt_state().row;
    }

    std::string written;

    HostLaunchOptions& launch_options()
    {
        return GridHostBase::launch_options();
    }

    int cols_ = 40;
    int rows_ = 10;

protected:
    std::string_view host_name() const override
    {
        return "pstest";
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

struct PsSetup
{
    PsFakeWindow window;
    PsFakeRenderer renderer;
    TextService text_service;
    PsTestHost host;
    draxul::tests::TestHostCallbacks callbacks;
    bool ok = false;

    explicit PsSetup(int cols = 40, int rows = 10)
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
};

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("terminal vt: ESC 7 saves cursor position", "[terminal]")
{
    PsSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    // Move cursor to (5, 3) then save with ESC 7.
    ts.host.feed("\x1B[4;6H"); // CUP row=4, col=6 (1-based → 3, 5)
    INFO("cursor at col 5 before save");
    REQUIRE(ts.host.col() == 5);
    INFO("cursor at row 3 before save");
    REQUIRE(ts.host.row() == 3);
    ts.host.feed("\x1B"
                 "7"); // ESC 7 = DECSC
    // Move elsewhere.
    ts.host.feed("\x1B[1;1H");
    INFO("cursor moved to 0,0");
    REQUIRE(ts.host.col() == 0);
    // Restore with ESC 8.
    ts.host.feed("\x1B"
                 "8"); // ESC 8 = DECRC
    INFO("cursor restored to col 5");
    REQUIRE(ts.host.col() == 5);
    INFO("cursor restored to row 3");
    REQUIRE(ts.host.row() == 3);
}

TEST_CASE("terminal vt: ESC 7/8 round-trip does not disturb grid content", "[terminal]")
{
    PsSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("Hello");
    ts.host.feed("\x1B"
                 "7"); // save at col 5
    ts.host.feed("\x1B[1;1H");
    ts.host.feed("World");
    ts.host.feed("\x1B"
                 "8"); // restore to col 5
    // Cursor back at (5,0); 'H'–'o' at columns 0–4, 'W'–'d' at 0–4 too.
    INFO("World overwrote Hello at col 0");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("W"));
    INFO("cursor restored to col 5");
    REQUIRE(ts.host.col() == 5);
}

TEST_CASE("terminal vt: PSReadLine bracketed paste wraps clipboard text", "[terminal]")
{
    PsSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.window.clipboard_ = "some text";
    // PSReadLine sends ?2004h to enable bracketed paste.
    ts.host.feed("\x1B[?2004h");
    ts.host.written.clear();
    ts.host.dispatch_action("paste");
    const std::string& out = ts.host.written;
    INFO("paste must have opening bracket");
    REQUIRE(out.find("\x1B[200~") != std::string::npos);
    INFO("paste must contain clipboard text");
    REQUIRE(out.find("some text") != std::string::npos);
    INFO("paste must have closing bracket");
    REQUIRE(out.find("\x1B[201~") != std::string::npos);
}

TEST_CASE("terminal vt: bracketed paste disabled when PSReadLine sends ?2004l", "[terminal]")
{
    PsSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.window.clipboard_ = "abc";
    ts.host.feed("\x1B[?2004h");
    ts.host.feed("\x1B[?2004l");
    ts.host.written.clear();
    ts.host.dispatch_action("paste");
    const std::string& out = ts.host.written;
    INFO("brackets must be absent when disabled");
    REQUIRE(out.find("\x1B[200~") == std::string::npos);
    INFO("clipboard text must still be pasted");
    REQUIRE(out.find("abc") != std::string::npos);
}

TEST_CASE("terminal vt: PSReadLine-style startup sequence renders prompt correctly", "[terminal]")
{
    PsSetup ts(80, 24);
    INFO("host must initialize");
    REQUIRE(ts.ok);

    // Typical PSReadLine preamble:
    //   hide cursor, go home, enable bracketed paste, save cursor
    ts.host.feed(
        "\x1B[?25l" // hide cursor
        "\x1B[1;1H" // CUP home
        "\x1B[?2004h" // enable bracketed paste
        "\x1B"
        "7" // DECSC - save cursor at (0,0)
    );

    // Write a coloured prompt "PS> " using SGR.
    ts.host.feed("\x1B[32mPS> \x1B[0m");

    // Restore cursor and show it — PSReadLine is now ready for input.
    ts.host.feed(
        "\x1B"
        "8" // DECRC - restore cursor to (0,0)
        "\x1B[?25h" // show cursor
    );

    // Prompt text should be visible on row 0.
    INFO("prompt 'P' at col 0");
    REQUIRE(ts.host.cell_text(0, 0) == std::string("P"));
    INFO("prompt 'S' at col 1");
    REQUIRE(ts.host.cell_text(1, 0) == std::string("S"));
    INFO("prompt '>' at col 2");
    REQUIRE(ts.host.cell_text(2, 0) == std::string(">"));
    // After ESC 8, cursor is back at (0,0).
    INFO("cursor restored to col 0 after ESC 8");
    REQUIRE(ts.host.col() == 0);
    INFO("cursor on row 0 after ESC 8");
    REQUIRE(ts.host.row() == 0);
}

// ---------------------------------------------------------------------------
// Paste confirmation / pending-paste-overwrite tests (WI 05)
// ---------------------------------------------------------------------------

TEST_CASE("terminal vt: double large paste warns about overwrite", "[terminal][paste]")
{
    PsSetup ts;
    REQUIRE(ts.ok);

    // Set paste_confirm_lines to 3 so pastes with >= 3 lines trigger confirm.
    ts.host.launch_options().paste_confirm_lines = 3;

    // First large paste (4 lines = 3 newlines).
    ts.window.clipboard_ = "line1\nline2\nline3\nline4";
    ts.host.dispatch_action("paste");
    INFO("first large paste must trigger a confirmation toast");
    REQUIRE(ts.callbacks.push_toast_calls == 1);
    REQUIRE(ts.callbacks.last_toast_message.find("Paste 4 lines?") != std::string::npos);

    // Second large paste while first is still pending.
    ts.window.clipboard_ = "a\nb\nc\nd\ne";
    ts.host.dispatch_action("paste");
    INFO("second large paste must warn about replacement");
    REQUIRE(ts.callbacks.push_toast_calls == 3); // replacement warning + new confirmation
    // The replacement warning toast.
    // Note: push_toast_calls == 3 because: 1st confirm + replacement warning + 2nd confirm.
    // The last toast is the new confirmation.
    REQUIRE(ts.callbacks.last_toast_message.find("Paste 5 lines?") != std::string::npos);
}

TEST_CASE("terminal vt: confirm_paste sends new payload after overwrite", "[terminal][paste]")
{
    PsSetup ts;
    REQUIRE(ts.ok);
    ts.host.launch_options().paste_confirm_lines = 2;

    // First large paste.
    ts.window.clipboard_ = "aaa\nbbb";
    ts.host.dispatch_action("paste");
    // Overwrite with second large paste.
    ts.window.clipboard_ = "xxx\nyyy";
    ts.host.dispatch_action("paste");

    // Confirm — should send the SECOND paste, not the first.
    ts.host.written.clear();
    ts.host.dispatch_action("confirm_paste");
    INFO("confirm must send the second (replacement) payload");
    REQUIRE(ts.host.written.find("xxx\nyyy") != std::string::npos);
    REQUIRE(ts.host.written.find("aaa\nbbb") == std::string::npos);
}

TEST_CASE("terminal vt: single large paste has no overwrite warning", "[terminal][paste]")
{
    PsSetup ts;
    REQUIRE(ts.ok);
    ts.host.launch_options().paste_confirm_lines = 2;

    ts.window.clipboard_ = "line1\nline2";
    ts.host.dispatch_action("paste");
    INFO("single large paste should only show confirmation, no overwrite warning");
    REQUIRE(ts.callbacks.push_toast_calls == 1);
    REQUIRE(ts.callbacks.last_toast_message.find("replaced") == std::string::npos);
}
