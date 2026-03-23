#include "support/fake_renderer.h"
#include "support/fake_window.h"

#include <draxul/terminal_host_base.h>
#include <draxul/terminal_key_encoder.h>
#include <draxul/vt_state.h>

#include <catch2/catch_all.hpp>
#include <draxul/host.h>
#include <draxul/renderer.h>
#include <draxul/text_service.h>
#include <draxul/window.h>

#include <SDL3/SDL_keycode.h>
#include <filesystem>
#include <string>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

// ---------------------------------------------------------------------------
// TestableKeyHost — minimal terminal host for encode_key testing.
// Captures on_key output via do_process_write into the `written` member.
// ---------------------------------------------------------------------------

class TestableKeyHost final : public TerminalHostBase
{
public:
    std::string written; // bytes sent back to the "process" by on_key

    // Feed raw VT bytes into the parser (to drive mode changes such as DECCKM).
    void feed(std::string_view bytes)
    {
        consume_output(bytes);
    }

protected:
    std::string_view host_name() const override
    {
        return "test-key";
    }

    bool initialize_host() override
    {
        highlights().set_default_fg({ 1.0f, 1.0f, 1.0f, 1.0f });
        highlights().set_default_bg({ 0.0f, 0.0f, 0.0f, 1.0f });
        apply_grid_size(20, 5);
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
// Setup helper — mirrors TermSetup in terminal_vt_tests.cpp.
// ---------------------------------------------------------------------------

struct KeyTestSetup
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    TestableKeyHost host;
    bool ok = false;

    KeyTestSetup()
    {
        TextServiceConfig ts_cfg;
        ts_cfg.font_path
            = (std::filesystem::path(DRAXUL_PROJECT_ROOT) / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf").string();
        text_service.initialize(ts_cfg, TextService::DEFAULT_POINT_SIZE, 96.0f);

        HostViewport vp;
        vp.grid_size.x = 20;
        vp.grid_size.y = 5;

        HostContext ctx{ window, renderer, text_service, {}, vp, 96.0f };

        HostCallbacks cbs;
        cbs.request_frame = [] {};
        cbs.request_quit = [] {};
        cbs.wake_window = [] {};
        cbs.set_window_title = [](const std::string&) {};
        cbs.set_text_input_area = [](int, int, int, int) {};

        ok = host.initialize(ctx, std::move(cbs));
    }

    // Simulate a key press and return the bytes written to the process.
    std::string press(int keycode, ModifierFlags mod = kModNone)
    {
        host.written.clear();
        host.on_key({ 0, keycode, mod, true });
        return host.written;
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// -----------------------------------------------------------------------
// Arrow keys — normal cursor mode (default)
// -----------------------------------------------------------------------

TEST_CASE("encode_key: up arrow emits CSI A in normal mode", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("up arrow = CSI A");
    REQUIRE(s.press(SDLK_UP) == std::string("\x1B[A"));
}

TEST_CASE("encode_key: down arrow emits CSI B in normal mode", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("down arrow = CSI B");
    REQUIRE(s.press(SDLK_DOWN) == std::string("\x1B[B"));
}

TEST_CASE("encode_key: right arrow emits CSI C in normal mode", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("right arrow = CSI C");
    REQUIRE(s.press(SDLK_RIGHT) == std::string("\x1B[C"));
}

TEST_CASE("encode_key: left arrow emits CSI D in normal mode", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("left arrow = CSI D");
    REQUIRE(s.press(SDLK_LEFT) == std::string("\x1B[D"));
}

// -----------------------------------------------------------------------
// DECCKM — application cursor mode
// -----------------------------------------------------------------------

TEST_CASE("encode_key: DECCKM enable switches arrows to SS3 sequences", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    s.host.feed("\x1B[?1h"); // enable DECCKM
    INFO("up in app mode = SS3 A");
    REQUIRE(s.press(SDLK_UP) == std::string("\x1BOA"));
    INFO("down in app mode = SS3 B");
    REQUIRE(s.press(SDLK_DOWN) == std::string("\x1BOB"));
    INFO("right in app mode = SS3 C");
    REQUIRE(s.press(SDLK_RIGHT) == std::string("\x1BOC"));
    INFO("left in app mode = SS3 D");
    REQUIRE(s.press(SDLK_LEFT) == std::string("\x1BOD"));
}

TEST_CASE("encode_key: DECCKM disable returns arrows to CSI sequences", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    s.host.feed("\x1B[?1h"); // enable DECCKM
    s.host.feed("\x1B[?1l"); // disable DECCKM
    INFO("up back to CSI A");
    REQUIRE(s.press(SDLK_UP) == std::string("\x1B[A"));
    INFO("down back to CSI B");
    REQUIRE(s.press(SDLK_DOWN) == std::string("\x1B[B"));
    INFO("right back to CSI C");
    REQUIRE(s.press(SDLK_RIGHT) == std::string("\x1B[C"));
    INFO("left back to CSI D");
    REQUIRE(s.press(SDLK_LEFT) == std::string("\x1B[D"));
}

// -----------------------------------------------------------------------
// Navigation keys
// -----------------------------------------------------------------------

TEST_CASE("encode_key: Home emits CSI H", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("home = CSI H");
    REQUIRE(s.press(SDLK_HOME) == std::string("\x1B[H"));
}

TEST_CASE("encode_key: End emits CSI F", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("end = CSI F");
    REQUIRE(s.press(SDLK_END) == std::string("\x1B[F"));
}

TEST_CASE("encode_key: Insert emits CSI 2~", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("insert = CSI 2~");
    REQUIRE(s.press(SDLK_INSERT) == std::string("\x1B[2~"));
}

TEST_CASE("encode_key: Delete emits CSI 3~", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("delete = CSI 3~");
    REQUIRE(s.press(SDLK_DELETE) == std::string("\x1B[3~"));
}

TEST_CASE("encode_key: Page Up emits CSI 5~", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("page up = CSI 5~");
    REQUIRE(s.press(SDLK_PAGEUP) == std::string("\x1B[5~"));
}

TEST_CASE("encode_key: Page Down emits CSI 6~", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("page down = CSI 6~");
    REQUIRE(s.press(SDLK_PAGEDOWN) == std::string("\x1B[6~"));
}

// -----------------------------------------------------------------------
// Common keys
// -----------------------------------------------------------------------

TEST_CASE("encode_key: Tab emits 0x09", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("tab = 0x09");
    REQUIRE(s.press(SDLK_TAB) == std::string("\x09"));
}

TEST_CASE("encode_key: Enter emits carriage return 0x0D", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("enter = 0x0D");
    REQUIRE(s.press(SDLK_RETURN) == std::string("\x0D"));
}

TEST_CASE("encode_key: Escape emits 0x1B", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("escape = 0x1B");
    REQUIRE(s.press(SDLK_ESCAPE) == std::string("\x1B"));
}

TEST_CASE("encode_key: Backspace emits DEL 0x7F", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("backspace = 0x7F");
    REQUIRE(s.press(SDLK_BACKSPACE) == std::string("\x7F"));
}

// -----------------------------------------------------------------------
// F-keys (xterm standard sequences)
// -----------------------------------------------------------------------

TEST_CASE("encode_key: F1 emits SS3 P", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("F1 = ESC O P");
    REQUIRE(s.press(SDLK_F1) == std::string("\x1BOP"));
}

TEST_CASE("encode_key: F2 emits SS3 Q", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("F2 = ESC O Q");
    REQUIRE(s.press(SDLK_F2) == std::string("\x1BOQ"));
}

TEST_CASE("encode_key: F3 emits SS3 R", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("F3 = ESC O R");
    REQUIRE(s.press(SDLK_F3) == std::string("\x1BOR"));
}

TEST_CASE("encode_key: F4 emits SS3 S", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("F4 = ESC O S");
    REQUIRE(s.press(SDLK_F4) == std::string("\x1BOS"));
}

TEST_CASE("encode_key: F5 emits CSI 15~", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("F5 = CSI 15~");
    REQUIRE(s.press(SDLK_F5) == std::string("\x1B[15~"));
}

TEST_CASE("encode_key: F6 emits CSI 17~", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("F6 = CSI 17~");
    REQUIRE(s.press(SDLK_F6) == std::string("\x1B[17~"));
}

TEST_CASE("encode_key: F7 emits CSI 18~", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("F7 = CSI 18~");
    REQUIRE(s.press(SDLK_F7) == std::string("\x1B[18~"));
}

TEST_CASE("encode_key: F8 emits CSI 19~", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("F8 = CSI 19~");
    REQUIRE(s.press(SDLK_F8) == std::string("\x1B[19~"));
}

TEST_CASE("encode_key: F9 emits CSI 20~", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("F9 = CSI 20~");
    REQUIRE(s.press(SDLK_F9) == std::string("\x1B[20~"));
}

TEST_CASE("encode_key: F10 emits CSI 21~", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("F10 = CSI 21~");
    REQUIRE(s.press(SDLK_F10) == std::string("\x1B[21~"));
}

TEST_CASE("encode_key: F11 emits CSI 23~", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("F11 = CSI 23~");
    REQUIRE(s.press(SDLK_F11) == std::string("\x1B[23~"));
}

TEST_CASE("encode_key: F12 emits CSI 24~", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("F12 = CSI 24~");
    REQUIRE(s.press(SDLK_F12) == std::string("\x1B[24~"));
}

// -----------------------------------------------------------------------
// Alt+letter / Alt+digit
// -----------------------------------------------------------------------

TEST_CASE("encode_key: Alt+a emits ESC a", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("Alt+a = ESC a");
    REQUIRE(s.press(SDLK_A, kModAlt) == std::string("\x1B") + "a");
}

TEST_CASE("encode_key: Alt+z emits ESC z", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("Alt+z = ESC z");
    REQUIRE(s.press(SDLK_Z, kModAlt) == std::string("\x1B") + "z");
}

TEST_CASE("encode_key: Alt+0 emits ESC 0", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("Alt+0 = ESC 0");
    REQUIRE(s.press(SDLK_0, kModAlt) == std::string("\x1B") + "0");
}

TEST_CASE("encode_key: right Alt modifier also triggers Alt+letter prefix", "[input]")
{
    KeyTestSetup s;
    INFO("host must initialize");
    REQUIRE(s.ok);
    INFO("right-Alt+b = ESC b");
    REQUIRE(s.press(SDLK_B, kModAlt) == std::string("\x1B") + "b");
}

// -----------------------------------------------------------------------
// Exhaustive data-driven tests using encode_terminal_key() directly.
// These bypass the full host setup to test the encoder in isolation.
// -----------------------------------------------------------------------

// -----------------------------------------------------------------------
// All mapped special keys in normal cursor mode
// -----------------------------------------------------------------------

TEST_CASE("encode_key: exhaustive — all mapped special keys produce correct sequences", "[input]")
{
    struct Entry
    {
        int keycode;
        const char* expected;
        const char* label;
    };
    static const Entry kMapped[] = {
        { SDLK_RETURN, "\r", "RETURN" },
        { SDLK_TAB, "\t", "TAB" },
        { SDLK_ESCAPE, "\x1B", "ESCAPE" },
        { SDLK_BACKSPACE, "\x7F", "BACKSPACE" },
        { SDLK_UP, "\x1B[A", "UP" },
        { SDLK_DOWN, "\x1B[B", "DOWN" },
        { SDLK_RIGHT, "\x1B[C", "RIGHT" },
        { SDLK_LEFT, "\x1B[D", "LEFT" },
        { SDLK_HOME, "\x1B[H", "HOME" },
        { SDLK_END, "\x1B[F", "END" },
        { SDLK_INSERT, "\x1B[2~", "INSERT" },
        { SDLK_DELETE, "\x1B[3~", "DELETE" },
        { SDLK_PAGEUP, "\x1B[5~", "PAGEUP" },
        { SDLK_PAGEDOWN, "\x1B[6~", "PAGEDOWN" },
        { SDLK_F1, "\x1BOP", "F1" },
        { SDLK_F2, "\x1BOQ", "F2" },
        { SDLK_F3, "\x1BOR", "F3" },
        { SDLK_F4, "\x1BOS", "F4" },
        { SDLK_F5, "\x1B[15~", "F5" },
        { SDLK_F6, "\x1B[17~", "F6" },
        { SDLK_F7, "\x1B[18~", "F7" },
        { SDLK_F8, "\x1B[19~", "F8" },
        { SDLK_F9, "\x1B[20~", "F9" },
        { SDLK_F10, "\x1B[21~", "F10" },
        { SDLK_F11, "\x1B[23~", "F11" },
        { SDLK_F12, "\x1B[24~", "F12" },
    };

    VtState vt;
    for (const auto& e : kMapped)
    {
        KeyEvent evt{ 0, e.keycode, kModNone, true };
        auto result = encode_terminal_key(evt, vt);
        INFO((std::string(e.label) + " must produce a non-empty sequence").c_str());
        REQUIRE(!result.empty());
        INFO((std::string(e.label) + " sequence is correct").c_str());
        REQUIRE(result == std::string(e.expected));
    }
}

// -----------------------------------------------------------------------
// Ctrl+a through Ctrl+z produce control characters 1-26
// -----------------------------------------------------------------------

TEST_CASE("encode_key: exhaustive — Ctrl+a through Ctrl+z produce control characters 1-26", "[input]")
{
    VtState vt;
    for (int i = 0; i < 26; ++i)
    {
        int keycode = SDLK_A + i; // SDL3 lowercase letters a-z
        KeyEvent evt{ 0, keycode, kModCtrl, true };
        auto result = encode_terminal_key(evt, vt);
        char expected = static_cast<char>(i + 1);
        INFO((std::string("Ctrl+") + static_cast<char>('a' + i) + " must produce control char").c_str());
        REQUIRE(!result.empty());
        INFO((std::string("Ctrl+") + static_cast<char>('a' + i) + " = ^" + static_cast<char>('A' + i)).c_str());
        REQUIRE(result == std::string(1, expected));
    }
}

// -----------------------------------------------------------------------
// Alt+printable ASCII (0x20-0x7E) produces ESC-prefixed character
// -----------------------------------------------------------------------

TEST_CASE("encode_key: exhaustive — Alt+printable ASCII produces ESC-prefixed character", "[input]")
{
    VtState vt;
    for (int keycode = 0x20; keycode <= 0x7E; ++keycode)
    {
        // Skip keycodes that are also handled by Ctrl branch (a-z with kModCtrl+kModAlt
        // could conflict; here we test Alt alone).
        KeyEvent evt{ 0, keycode, kModAlt, true };
        auto result = encode_terminal_key(evt, vt);
        std::string expected = std::string("\x1B") + static_cast<char>(keycode);
        INFO((std::string("Alt+0x") + std::to_string(keycode) + " must be non-empty").c_str());
        REQUIRE(!result.empty());
        INFO((std::string("Alt+0x") + std::to_string(keycode) + " = ESC + char").c_str());
        REQUIRE(result == expected);
    }
}

// -----------------------------------------------------------------------
// Printable ASCII without modifier returns empty (SDL text-input path)
// -----------------------------------------------------------------------

TEST_CASE("encode_key: exhaustive — printable ASCII without modifier returns empty", "[input]")
{
    // Printable keys (space through ~) with no modifier are intentionally
    // not encoded here; SDL fires SDL_EVENT_TEXT_INPUT for them instead.
    VtState vt;
    for (int keycode = 0x20; keycode <= 0x7E; ++keycode)
    {
        KeyEvent evt{ 0, keycode, kModNone, true };
        auto result = encode_terminal_key(evt, vt);
        INFO((std::string("printable 0x") + std::to_string(keycode)
            + " without modifier must return empty (text-input path)")
                .c_str());
        REQUIRE(result.empty());
    }
}

// -----------------------------------------------------------------------
// Modifier-only keys return empty
// -----------------------------------------------------------------------

TEST_CASE("encode_key: exhaustive — modifier-only keycodes return empty", "[input]")
{
    // Pressing a modifier key alone never produces a VT sequence.
    static const int kModifierKeys[] = {
        SDLK_LSHIFT,
        SDLK_RSHIFT,
        SDLK_LCTRL,
        SDLK_RCTRL,
        SDLK_LALT,
        SDLK_RALT,
        SDLK_LGUI,
        SDLK_RGUI,
        SDLK_CAPSLOCK,
    };
    VtState vt;
    for (int key : kModifierKeys)
    {
        KeyEvent evt{ 0, key, kModNone, true };
        auto result = encode_terminal_key(evt, vt);
        INFO("modifier-only key must return empty");
        REQUIRE(result.empty());
    }
}

// -----------------------------------------------------------------------
// F13-F24 are intentionally unmapped in the current encoder
// -----------------------------------------------------------------------

TEST_CASE("encode_key: exhaustive — F13-F24 are currently unmapped (return empty)", "[input]")
{
    // F13-F24 are not yet encoded by encode_terminal_key. These keys are
    // uncommon and Neovim does not standardise their sequences, so they
    // intentionally fall through to empty string. This test documents the
    // current behaviour; add entries to the encoder when support is needed.
    static const int kF13_F24[] = {
        SDLK_F13,
        SDLK_F14,
        SDLK_F15,
        SDLK_F16,
        SDLK_F17,
        SDLK_F18,
        SDLK_F19,
        SDLK_F20,
        SDLK_F21,
        SDLK_F22,
        SDLK_F23,
        SDLK_F24,
    };
    VtState vt;
    for (int key : kF13_F24)
    {
        KeyEvent evt{ 0, key, kModNone, true };
        auto result = encode_terminal_key(evt, vt);
        INFO("F13-F24 must return empty (intentionally unmapped)");
        REQUIRE(result.empty());
    }
}

// -----------------------------------------------------------------------
// F1-F12 modifier combos: current encoder returns the unmodified sequence
// (modifier differentiation not yet implemented — documented here)
// -----------------------------------------------------------------------

TEST_CASE("encode_key: exhaustive — F1-F12 with Shift/Ctrl/Alt return base sequence", "[input]")
{
    // The encoder does not yet differentiate modifier combos on F-keys.
    // Shift+F1, Ctrl+F1, and Alt+F1 all produce the same "\x1BOP" as
    // plain F1.  This test pins the current behaviour; update it when
    // modified F-key sequences are implemented.
    struct FKey
    {
        int keycode;
        const char* base_seq;
    };
    static const FKey kFKeys[] = {
        { SDLK_F1, "\x1BOP" },
        { SDLK_F2, "\x1BOQ" },
        { SDLK_F3, "\x1BOR" },
        { SDLK_F4, "\x1BOS" },
        { SDLK_F5, "\x1B[15~" },
        { SDLK_F6, "\x1B[17~" },
        { SDLK_F7, "\x1B[18~" },
        { SDLK_F8, "\x1B[19~" },
        { SDLK_F9, "\x1B[20~" },
        { SDLK_F10, "\x1B[21~" },
        { SDLK_F11, "\x1B[23~" },
        { SDLK_F12, "\x1B[24~" },
    };
    static const ModifierFlags kMods[] = { kModShift, kModCtrl, kModAlt };

    VtState vt;
    for (const auto& fk : kFKeys)
    {
        for (ModifierFlags mod : kMods)
        {
            KeyEvent evt{ 0, fk.keycode, mod, true };
            auto result = encode_terminal_key(evt, vt);
            INFO("modified F-key currently maps to same base sequence as unmodified");
            REQUIRE(result == std::string(fk.base_seq));
        }
    }
}

// -----------------------------------------------------------------------
// Numpad keys are currently unmapped (return empty)
// -----------------------------------------------------------------------

TEST_CASE("encode_key: exhaustive — numpad keys are currently unmapped (return empty)", "[input]")
{
    // Numpad keycodes are not yet handled by encode_terminal_key.
    // This test documents the gap; applications keys via numpad may be
    // sent as text-input events when NumLock is active.
    static const int kNumpad[] = {
        SDLK_KP_0,
        SDLK_KP_1,
        SDLK_KP_2,
        SDLK_KP_3,
        SDLK_KP_4,
        SDLK_KP_5,
        SDLK_KP_6,
        SDLK_KP_7,
        SDLK_KP_8,
        SDLK_KP_9,
        SDLK_KP_ENTER,
        SDLK_KP_PLUS,
        SDLK_KP_MINUS,
        SDLK_KP_MULTIPLY,
        SDLK_KP_DIVIDE,
        SDLK_KP_PERIOD,
    };
    VtState vt;
    for (int key : kNumpad)
    {
        KeyEvent evt{ 0, key, kModNone, true };
        auto result = encode_terminal_key(evt, vt);
        INFO("numpad key must return empty (intentionally unmapped)");
        REQUIRE(result.empty());
    }
}
