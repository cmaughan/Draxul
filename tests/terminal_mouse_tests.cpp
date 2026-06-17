#include "support/test_host_callbacks.h"
#include "support/test_local_terminal_host.h"
#include "support/test_terminal_host_fixture.h"

#include <catch2/catch_all.hpp>

#include <string>

using namespace draxul;
using namespace draxul::tests;

namespace
{

struct MouseTerminalSetup : TerminalHostFixture<TestLocalTerminalHost>
{
    explicit MouseTerminalSetup(int cols = 20, int rows = 5)
        : TerminalHostFixture(cols, rows)
    {
    }

    void press(int button, int px, int py, int mod = 0)
    {
        MouseButtonEvent ev;
        ev.button = button;
        ev.pressed = true;
        ev.pos = { px, py };
        ev.mod = static_cast<uint16_t>(mod);
        host.on_mouse_button(ev);
    }

    void release(int button, int px, int py, int mod = 0)
    {
        MouseButtonEvent ev;
        ev.button = button;
        ev.pressed = false;
        ev.pos = { px, py };
        ev.mod = static_cast<uint16_t>(mod);
        host.on_mouse_button(ev);
    }

    void move(int px, int py, int mod = 0)
    {
        MouseMoveEvent ev;
        ev.pos = { px, py };
        ev.mod = static_cast<uint16_t>(mod);
        host.on_mouse_move(ev);
    }

    void wheel(int px, int py, float dy, int mod = 0)
    {
        MouseWheelEvent ev;
        ev.pos = { px, py };
        ev.delta = { 0.0f, dy };
        ev.mod = static_cast<uint16_t>(mod);
        host.on_mouse_wheel(ev);
    }
};

// ---------------------------------------------------------------------------
// Helpers to decode mouse report bytes from the written buffer.
// ---------------------------------------------------------------------------

// Check whether 'written' contains an X10 (non-SGR) mouse report.
bool has_x10_report(const std::string& written)
{
    return written.size() >= 6 && written[0] == '\x1B' && written[1] == '[' && written[2] == 'M';
}

// Check whether 'written' contains an SGR mouse report starting with ESC[<.
bool has_sgr_report(const std::string& written)
{
    return written.size() >= 4 && written[0] == '\x1B' && written[1] == '[' && written[2] == '<';
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("mouse: no mode — press produces no output", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.press(1, 0, 0);
    INFO("no output when mouse mode is None");
    REQUIRE(ts.host.written.empty());
}

TEST_CASE("mouse: no mode — motion produces no output", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.press(1, 0, 0);
    ts.move(8, 16);
    // Only the Draxul selection path is active; that writes nothing.
    INFO("no output when mouse mode is None");
    REQUIRE(ts.host.written.empty());
}

TEST_CASE("mouse: no mode — release produces no output", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.press(1, 0, 0);
    ts.release(1, 0, 0);
    INFO("no output when mouse mode is None");
    REQUIRE(ts.host.written.empty());
}

TEST_CASE("mouse: local terminal host ignores fractional renderer scroll offsets", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    REQUIRE(ts.renderer.last_handle != nullptr);

    ts.host.set_scroll_offset(7.5f);

    INFO("shell hosts render at whole-cell positions to avoid touchpad jitter");
    REQUIRE(ts.renderer.last_handle->last_scroll_offset == Catch::Approx(0.0f));
}

TEST_CASE("mouse: OSC 8 hyperlink opens on plain left press", "[terminal][osc8]")
{
    MouseTerminalSetup ts;
    REQUIRE(ts.ok);

    ts.host.feed("\x1B]8;;https://example.com\x1B\\x\x1B]8;;\x1B\\");
    ts.press(1, 0, 0);

    REQUIRE(ts.window.opened_urls_.size() == 1);
    REQUIRE(ts.window.opened_urls_[0] == "https://example.com");
}

TEST_CASE("mouse: detected URL opens only with command modifier", "[terminal][url]")
{
    MouseTerminalSetup ts;
    REQUIRE(ts.ok);

    ts.host.feed("https://example.com");
    ts.host.flush_for_test();
    ts.press(1, 0, 0);
    REQUIRE(ts.window.opened_urls_.empty());

    ts.press(1, 0, 0, kModCtrl);
    REQUIRE(ts.window.opened_urls_.size() == 1);
    REQUIRE(ts.window.opened_urls_[0] == "https://example.com");
}

TEST_CASE("mouse: DECSET 1000 — button press emits X10 report", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[?1000h"); // enable button tracking
    ts.press(1, 0, 0);
    INFO("press should emit a report");
    REQUIRE(!ts.host.written.empty());
    INFO("report should be X10 or SGR format");
    REQUIRE((has_x10_report(ts.host.written) || has_sgr_report(ts.host.written)));
}

TEST_CASE("mouse: DECSET 1000 — button release emits report", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[?1000h");
    ts.press(1, 0, 0);
    ts.host.written.clear();
    ts.release(1, 0, 0);
    INFO("release should emit a report");
    REQUIRE(!ts.host.written.empty());
}

TEST_CASE("mouse: DECSET 1000 — motion produces no report", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[?1000h");
    ts.press(1, 0, 0);
    ts.host.written.clear();
    ts.move(8, 0);
    // DECSET 1000 (Button mode) does not emit motion events.
    INFO("motion must not emit in button mode");
    REQUIRE(ts.host.written.empty());
}

TEST_CASE("mouse: DECSET 1002 — press emits report", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[?1002h");
    ts.press(1, 0, 0);
    INFO("press emits in drag mode");
    REQUIRE(!ts.host.written.empty());
}

TEST_CASE("mouse: DECSET 1002 — motion without button held produces no output", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[?1002h");
    // Do not press any button; just move.
    ts.move(8, 0);
    INFO("motion without button held must not emit in drag mode");
    REQUIRE(ts.host.written.empty());
}

TEST_CASE("mouse: DECSET 1002 — motion with button held emits report", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[?1002h");
    ts.press(1, 0, 0);
    ts.host.written.clear();
    ts.move(8, 0); // move while button is held
    INFO("motion while button held must emit in drag mode");
    REQUIRE(!ts.host.written.empty());
}

TEST_CASE("mouse: DECSET 1002 — release then motion produces no output", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[?1002h");
    ts.press(1, 0, 0);
    ts.release(1, 0, 0);
    ts.host.written.clear();
    ts.move(8, 0);
    INFO("motion after release must not emit in drag mode");
    REQUIRE(ts.host.written.empty());
}

TEST_CASE("mouse: DECSET 1003 — motion without button emits report", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[?1003h");
    ts.move(8, 0);
    INFO("motion without button must emit in any-motion mode");
    REQUIRE(!ts.host.written.empty());
}

TEST_CASE("mouse: DECSET 1003 — motion with button held emits report", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[?1003h");
    ts.press(1, 0, 0);
    ts.host.written.clear();
    ts.move(8, 0);
    INFO("motion with button held must emit in any-motion mode");
    REQUIRE(!ts.host.written.empty());
}

TEST_CASE("mouse: SGR encoding used when DECSET 1006 is active", "[terminal]")
{
    MouseTerminalSetup ts(80, 30);
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[?1000h\x1B[?1006h");
    // Press at a position that would exceed X10 byte encoding limits (col >= 223)
    // Cell 0 is at pixel 0; with cell width 8, col 240 = pixel 1920.
    ts.press(1, 1920, 0);
    INFO("report should use SGR format for large coordinates");
    REQUIRE(has_sgr_report(ts.host.written));
}

TEST_CASE("mouse: SGR press and release use correct final chars", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[?1000h\x1B[?1006h");
    ts.press(1, 0, 0);
    // SGR press uses 'M' as final char.
    INFO("SGR press uses 'M'");
    REQUIRE(ts.host.written.find('M') != std::string::npos);
    ts.host.written.clear();
    ts.release(1, 0, 0);
    // SGR release uses 'm' as final char.
    INFO("SGR release uses 'm'");
    REQUIRE(ts.host.written.find('m') != std::string::npos);
}

TEST_CASE("mouse: drag report encodes modifier bits", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[?1002h\x1B[?1006h");

    ts.press(1, 0, 0, kModNone);
    ts.host.written.clear();
    ts.move(8, 0, kModShift | kModCtrl);

    INFO("drag should include Shift and Ctrl modifier bits");
    REQUIRE(ts.host.written == "\x1B[<52;2;1M");
}

TEST_CASE("mouse: wheel report encodes modifier bits", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[?1000h\x1B[?1006h");

    ts.wheel(0, 0, 1.0f, kModCtrl);

    INFO("wheel should include Ctrl modifier bit");
    REQUIRE(ts.host.written == "\x1B[<80;1;1M");
}

TEST_CASE("mouse: disabling DECSET 1003 reverts to no-output on motion", "[terminal]")
{
    MouseTerminalSetup ts;
    INFO("host must initialize");
    REQUIRE(ts.ok);
    ts.host.feed("\x1B[?1003h");
    ts.move(8, 0);
    INFO("motion emits while 1003 active");
    REQUIRE(!ts.host.written.empty());
    ts.host.feed("\x1B[?1003l"); // disable
    ts.host.written.clear();
    ts.move(16, 0);
    INFO("motion must not emit after 1003 disabled");
    REQUIRE(ts.host.written.empty());
}
