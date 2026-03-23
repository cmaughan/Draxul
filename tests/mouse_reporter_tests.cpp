// Direct unit tests for MouseReporter — tests the class in isolation without
// the full terminal host stack.  The existing terminal_mouse_tests.cpp covers
// integration through LocalTerminalHost; these tests exercise the encoding and
// mode-switching logic directly.

#include <draxul/input_types.h>
#include <draxul/mouse_reporter.h>

#include <catch2/catch_all.hpp>

#include <string>

using namespace draxul;

namespace
{

// A simple harness that captures bytes written by MouseReporter.
struct Harness
{
    std::string written;
    MouseReporter reporter{ [this](std::string_view sv) { written += sv; } };

    void clear()
    {
        written.clear();
    }
};

} // namespace

// ===== set_mode / mode query ================================================

TEST_CASE("mouse_reporter: initial mode is None", "[mouse_reporter]")
{
    Harness h;
    REQUIRE(h.reporter.mode() == MouseReporter::MouseMode::None);
}

TEST_CASE("mouse_reporter: set_mode 1000 enables Button mode", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    REQUIRE(h.reporter.mode() == MouseReporter::MouseMode::Button);
}

TEST_CASE("mouse_reporter: set_mode 1002 enables Drag mode", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1002, true);
    REQUIRE(h.reporter.mode() == MouseReporter::MouseMode::Drag);
}

TEST_CASE("mouse_reporter: set_mode 1003 enables All mode", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1003, true);
    REQUIRE(h.reporter.mode() == MouseReporter::MouseMode::All);
}

TEST_CASE("mouse_reporter: disabling a mode reverts to None", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1003, true);
    h.reporter.set_mode(1003, false);
    REQUIRE(h.reporter.mode() == MouseReporter::MouseMode::None);
}

TEST_CASE("mouse_reporter: set_mode 1006 does not change tracking mode", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1006, true);
    REQUIRE(h.reporter.mode() == MouseReporter::MouseMode::None);
}

TEST_CASE("mouse_reporter: unknown DECSET mode is ignored", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(9999, true);
    REQUIRE(h.reporter.mode() == MouseReporter::MouseMode::None);
}

// ===== Mode priority: later set_mode calls override earlier =================

TEST_CASE("mouse_reporter: mode 1003 overrides earlier 1000", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.set_mode(1003, true);
    REQUIRE(h.reporter.mode() == MouseReporter::MouseMode::All);
}

TEST_CASE("mouse_reporter: mode 1000 overrides earlier 1003", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1003, true);
    h.reporter.set_mode(1000, true);
    REQUIRE(h.reporter.mode() == MouseReporter::MouseMode::Button);
}

TEST_CASE("mouse_reporter: disabling 1000 reverts to None even if 1003 was set first", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1003, true);
    h.reporter.set_mode(1000, true);
    h.reporter.set_mode(1000, false);
    REQUIRE(h.reporter.mode() == MouseReporter::MouseMode::None);
}

// ===== reset ================================================================

TEST_CASE("mouse_reporter: reset clears mode and SGR flag", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1003, true);
    h.reporter.set_mode(1006, true);
    h.reporter.on_button(1, true, 0, 0, 0); // set pressed bitmask
    h.reporter.reset();
    REQUIRE(h.reporter.mode() == MouseReporter::MouseMode::None);

    // After reset, pressing should produce no output (mode is None).
    h.clear();
    h.reporter.on_button(1, true, 0, 5, 5);
    REQUIRE(h.written.empty());
}

// ===== on_button: no mode ===================================================

TEST_CASE("mouse_reporter: on_button returns false when mode is None", "[mouse_reporter]")
{
    Harness h;
    REQUIRE_FALSE(h.reporter.on_button(1, true, 0, 0, 0));
    REQUIRE(h.written.empty());
}

// ===== on_button: X10 encoding (no SGR) =====================================

TEST_CASE("mouse_reporter: X10 left press at (0,0)", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    REQUIRE(h.reporter.on_button(1, true, 0, 0, 0));

    // ESC [ M <button+32> <col+33> <row+33>
    REQUIRE(h.written.size() == 6);
    REQUIRE(h.written[0] == '\x1B');
    REQUIRE(h.written[1] == '[');
    REQUIRE(h.written[2] == 'M');
    REQUIRE(h.written[3] == char(0 + 32)); // left button = 0
    REQUIRE(h.written[4] == char(0 + 33)); // col 0
    REQUIRE(h.written[5] == char(0 + 33)); // row 0
}

TEST_CASE("mouse_reporter: X10 middle press", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.on_button(2, true, 0, 5, 10);

    REQUIRE(h.written.size() == 6);
    REQUIRE(h.written[3] == char(1 + 32)); // middle = 1
    REQUIRE(h.written[4] == char(5 + 33));
    REQUIRE(h.written[5] == char(10 + 33));
}

TEST_CASE("mouse_reporter: X10 right press", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.on_button(3, true, 0, 2, 3);

    REQUIRE(h.written.size() == 6);
    REQUIRE(h.written[3] == char(2 + 32)); // right = 2
}

TEST_CASE("mouse_reporter: X10 release uses button code 3", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.on_button(1, true, 0, 0, 0);
    h.clear();
    h.reporter.on_button(1, false, 0, 0, 0);

    REQUIRE(h.written.size() == 6);
    REQUIRE(h.written[3] == char(3 + 32)); // release = 3
}

TEST_CASE("mouse_reporter: X10 encoding skipped for col >= 223", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.on_button(1, true, 0, 223, 0);
    // col 223 exceeds the byte-encodable range (max 222), so no output.
    REQUIRE(h.written.empty());
}

TEST_CASE("mouse_reporter: X10 encoding skipped for row >= 223", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.on_button(1, true, 0, 0, 223);
    REQUIRE(h.written.empty());
}

TEST_CASE("mouse_reporter: X10 encoding works at col/row 222 (max valid)", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.on_button(1, true, 0, 222, 222);
    REQUIRE(h.written.size() == 6);
    REQUIRE(h.written[4] == char(222 + 33));
    REQUIRE(h.written[5] == char(222 + 33));
}

// ===== on_button: SGR encoding ==============================================

TEST_CASE("mouse_reporter: SGR left press at (0,0)", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.set_mode(1006, true);
    h.reporter.on_button(1, true, 0, 0, 0);

    // \033[<0;1;1M  (1-based col/row, 'M' for press)
    REQUIRE(h.written == "\x1B[<0;1;1M");
}

TEST_CASE("mouse_reporter: SGR middle press", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.set_mode(1006, true);
    h.reporter.on_button(2, true, 0, 5, 10);

    REQUIRE(h.written == "\x1B[<1;6;11M");
}

TEST_CASE("mouse_reporter: SGR right press", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.set_mode(1006, true);
    h.reporter.on_button(3, true, 0, 2, 3);

    REQUIRE(h.written == "\x1B[<2;3;4M");
}

TEST_CASE("mouse_reporter: SGR release uses 'm' final char", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.set_mode(1006, true);
    h.reporter.on_button(1, true, 0, 0, 0);
    h.clear();
    h.reporter.on_button(1, false, 0, 0, 0);

    // Release: button code 3 (non-SGR release code), 'm' final char.
    REQUIRE(h.written == "\x1B[<3;1;1m");
}

TEST_CASE("mouse_reporter: SGR handles large coordinates", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.set_mode(1006, true);
    h.reporter.on_button(1, true, 0, 500, 300);

    REQUIRE(h.written == "\x1B[<0;501;301M");
}

// ===== Modifier bits ========================================================

TEST_CASE("mouse_reporter: Shift modifier adds bit 4 to button code", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.set_mode(1006, true);
    h.reporter.on_button(1, true, static_cast<int>(kModShift), 0, 0);

    // button 0 | 4 (shift) = 4
    REQUIRE(h.written == "\x1B[<4;1;1M");
}

TEST_CASE("mouse_reporter: Alt modifier adds bit 8 to button code", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.set_mode(1006, true);
    h.reporter.on_button(1, true, static_cast<int>(kModAlt), 0, 0);

    // button 0 | 8 (alt) = 8
    REQUIRE(h.written == "\x1B[<8;1;1M");
}

TEST_CASE("mouse_reporter: Ctrl modifier adds bit 16 to button code", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.set_mode(1006, true);
    h.reporter.on_button(1, true, static_cast<int>(kModCtrl), 0, 0);

    // button 0 | 16 (ctrl) = 16
    REQUIRE(h.written == "\x1B[<16;1;1M");
}

TEST_CASE("mouse_reporter: Shift+Ctrl modifiers combine", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.set_mode(1006, true);
    h.reporter.on_button(1, true, static_cast<int>(kModShift | kModCtrl), 0, 0);

    // button 0 | 4 | 16 = 20
    REQUIRE(h.written == "\x1B[<20;1;1M");
}

// ===== on_button: invalid button numbers ====================================

TEST_CASE("mouse_reporter: button 0 (invalid) returns false", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    REQUIRE_FALSE(h.reporter.on_button(0, true, 0, 0, 0));
    REQUIRE(h.written.empty());
}

TEST_CASE("mouse_reporter: button 4 (out of range) returns false", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    REQUIRE_FALSE(h.reporter.on_button(4, true, 0, 0, 0));
    REQUIRE(h.written.empty());
}

// ===== on_move ==============================================================

TEST_CASE("mouse_reporter: on_move returns false in None mode", "[mouse_reporter]")
{
    Harness h;
    REQUIRE_FALSE(h.reporter.on_move(0, 5, 5));
    REQUIRE(h.written.empty());
}

TEST_CASE("mouse_reporter: on_move returns false in Button mode", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.on_button(1, true, 0, 0, 0);
    h.clear();
    REQUIRE_FALSE(h.reporter.on_move(0, 5, 5));
    REQUIRE(h.written.empty());
}

TEST_CASE("mouse_reporter: on_move in Drag mode without button held returns false", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1002, true);
    REQUIRE_FALSE(h.reporter.on_move(0, 5, 5));
    REQUIRE(h.written.empty());
}

TEST_CASE("mouse_reporter: on_move in Drag mode with left button held emits report", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1002, true);
    h.reporter.set_mode(1006, true);
    h.reporter.on_button(1, true, 0, 0, 0);
    h.clear();

    REQUIRE(h.reporter.on_move(0, 10, 5));
    // Motion code = 32 + held_btn(0) = 32
    REQUIRE(h.written == "\x1B[<32;11;6M");
}

TEST_CASE("mouse_reporter: on_move in Drag mode with middle button held", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1002, true);
    h.reporter.set_mode(1006, true);
    h.reporter.on_button(2, true, 0, 0, 0);
    h.clear();

    REQUIRE(h.reporter.on_move(0, 3, 2));
    // Motion code = 32 + held_btn(1) = 33
    REQUIRE(h.written == "\x1B[<33;4;3M");
}

TEST_CASE("mouse_reporter: on_move in All mode without button held emits code 35", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1003, true);
    h.reporter.set_mode(1006, true);

    REQUIRE(h.reporter.on_move(0, 7, 3));
    // All mode, no button: code 35
    REQUIRE(h.written == "\x1B[<35;8;4M");
}

TEST_CASE("mouse_reporter: on_move in All mode with button held uses button code", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1003, true);
    h.reporter.set_mode(1006, true);
    h.reporter.on_button(1, true, 0, 0, 0);
    h.clear();

    REQUIRE(h.reporter.on_move(0, 4, 2));
    // Motion code = 32 + 0 (left) = 32
    REQUIRE(h.written == "\x1B[<32;5;3M");
}

TEST_CASE("mouse_reporter: on_move with modifiers in Drag mode", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1002, true);
    h.reporter.set_mode(1006, true);
    h.reporter.on_button(1, true, 0, 0, 0);
    h.clear();

    REQUIRE(h.reporter.on_move(static_cast<int>(kModShift | kModCtrl), 1, 0));
    // Motion code = 32 | 4 (shift) | 16 (ctrl) = 52
    REQUIRE(h.written == "\x1B[<52;2;1M");
}

TEST_CASE("mouse_reporter: on_move in Drag mode after release returns false", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1002, true);
    h.reporter.on_button(1, true, 0, 0, 0);
    h.reporter.on_button(1, false, 0, 0, 0);
    h.clear();

    REQUIRE_FALSE(h.reporter.on_move(0, 5, 5));
    REQUIRE(h.written.empty());
}

// ===== on_wheel =============================================================

TEST_CASE("mouse_reporter: wheel up emits correct SGR sequence", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.set_mode(1006, true);

    h.reporter.on_wheel(64, 0, 5, 3);
    // Wheel up = button code 64
    REQUIRE(h.written == "\x1B[<64;6;4M");
}

TEST_CASE("mouse_reporter: wheel down emits correct SGR sequence", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.set_mode(1006, true);

    h.reporter.on_wheel(65, 0, 5, 3);
    REQUIRE(h.written == "\x1B[<65;6;4M");
}

TEST_CASE("mouse_reporter: wheel with Ctrl modifier", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.set_mode(1006, true);

    h.reporter.on_wheel(64, static_cast<int>(kModCtrl), 0, 0);
    // 64 | 16 = 80
    REQUIRE(h.written == "\x1B[<80;1;1M");
}

TEST_CASE("mouse_reporter: wheel X10 encoding", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    // No SGR — use X10

    h.reporter.on_wheel(64, 0, 5, 3);
    REQUIRE(h.written.size() == 6);
    REQUIRE(h.written[0] == '\x1B');
    REQUIRE(h.written[1] == '[');
    REQUIRE(h.written[2] == 'M');
    REQUIRE(h.written[3] == char(64 + 32));
    REQUIRE(h.written[4] == char(5 + 33));
    REQUIRE(h.written[5] == char(3 + 33));
}

// ===== Button pressed bitmask tracking ======================================

TEST_CASE("mouse_reporter: pressing multiple buttons tracks bitmask correctly", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1002, true);
    h.reporter.set_mode(1006, true);

    // Press left, then right.
    h.reporter.on_button(1, true, 0, 0, 0);
    h.reporter.on_button(3, true, 0, 0, 0);
    h.clear();

    // Move — should report lowest held button (left = 0).
    h.reporter.on_move(0, 5, 5);
    REQUIRE(h.written == "\x1B[<32;6;6M");
    h.clear();

    // Release left, move again — now right (2) should be lowest.
    // Note: right button is button 3 (index 2), so held_btn = 2.
    h.reporter.on_button(1, false, 0, 0, 0);
    h.clear();
    h.reporter.on_move(0, 5, 5);
    // Motion code = 32 + 2 = 34
    REQUIRE(h.written == "\x1B[<34;6;6M");
}

// ===== SGR disabled after being enabled =====================================

TEST_CASE("mouse_reporter: disabling SGR reverts to X10 format", "[mouse_reporter]")
{
    Harness h;
    h.reporter.set_mode(1000, true);
    h.reporter.set_mode(1006, true);
    h.reporter.on_button(1, true, 0, 0, 0);
    REQUIRE(h.written.substr(0, 3) == "\x1B[<");
    h.clear();

    h.reporter.set_mode(1006, false);
    h.reporter.on_button(1, false, 0, 0, 0);
    // Should be X10 format now.
    REQUIRE(h.written.size() == 6);
    REQUIRE(h.written[0] == '\x1B');
    REQUIRE(h.written[1] == '[');
    REQUIRE(h.written[2] == 'M');
}
