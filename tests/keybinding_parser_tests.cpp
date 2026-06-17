#include "support/test_support.h"

#include <SDL3/SDL.h>
#include <catch2/catch_all.hpp>
#include <draxul/app_config_types.h>
#include <draxul/events.h>
#include <draxul/keybinding_parser.h>

using namespace draxul;

// ---------------------------------------------------------------------------
// parse_gui_keybinding: full roundtrip string → GuiKeybinding
// ---------------------------------------------------------------------------

TEST_CASE("keybinding parser: simple key with single modifier", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("copy", "Ctrl+C");
    REQUIRE(result.has_value());
    CHECK(result->action == "copy");
    CHECK(result->key == SDLK_C);
    CHECK(result->modifiers == kModCtrl);
    CHECK(result->prefix_key == 0);
}

TEST_CASE("keybinding parser: two modifiers Ctrl+Shift", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("copy", "Ctrl+Shift+C");
    REQUIRE(result.has_value());
    CHECK(result->key == SDLK_C);
    CHECK((result->modifiers & kModCtrl) != 0);
    CHECK((result->modifiers & kModShift) != 0);
}

TEST_CASE("keybinding parser: three modifiers Ctrl+Shift+Alt", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("copy", "Ctrl+Shift+Alt+C");
    REQUIRE(result.has_value());
    CHECK((result->modifiers & kModCtrl) != 0);
    CHECK((result->modifiers & kModShift) != 0);
    CHECK((result->modifiers & kModAlt) != 0);
}

TEST_CASE("keybinding parser: no modifier, bare key", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("toggle_diagnostics", "F12");
    REQUIRE(result.has_value());
    CHECK(result->key == SDLK_F12);
    CHECK(result->modifiers == kModNone);
}

// ---------------------------------------------------------------------------
// parse_modifier_token: case variations
// ---------------------------------------------------------------------------

TEST_CASE("keybinding parser: modifier case insensitive — ctrl", "[keybinding][parser]")
{
    // All case variations of ctrl should parse identically.
    auto lower = parse_gui_keybinding("copy", "ctrl+C");
    auto upper = parse_gui_keybinding("copy", "CTRL+C");
    auto mixed = parse_gui_keybinding("copy", "Ctrl+C");
    REQUIRE(lower.has_value());
    REQUIRE(upper.has_value());
    REQUIRE(mixed.has_value());
    CHECK(lower->modifiers == upper->modifiers);
    CHECK(lower->modifiers == mixed->modifiers);
    CHECK((lower->modifiers & kModCtrl) != 0);
}

TEST_CASE("keybinding parser: modifier 'control' alias", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("copy", "Control+C");
    REQUIRE(result.has_value());
    CHECK((result->modifiers & kModCtrl) != 0);
    CHECK(result->key == SDLK_C);
}

TEST_CASE("keybinding parser: modifier case insensitive — shift", "[keybinding][parser]")
{
    auto lower = parse_gui_keybinding("copy", "shift+C");
    auto upper = parse_gui_keybinding("copy", "SHIFT+C");
    REQUIRE(lower.has_value());
    REQUIRE(upper.has_value());
    CHECK((lower->modifiers & kModShift) != 0);
    CHECK(lower->modifiers == upper->modifiers);
}

TEST_CASE("keybinding parser: modifier case insensitive — alt", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("copy", "ALT+C");
    REQUIRE(result.has_value());
    CHECK((result->modifiers & kModAlt) != 0);
}

TEST_CASE("keybinding parser: super/gui/meta all map to kModSuper", "[keybinding][parser]")
{
    auto super_result = parse_gui_keybinding("copy", "Super+C");
    auto gui_result = parse_gui_keybinding("copy", "Gui+C");
    auto meta_result = parse_gui_keybinding("copy", "Meta+C");
    REQUIRE(super_result.has_value());
    REQUIRE(gui_result.has_value());
    REQUIRE(meta_result.has_value());
    CHECK((super_result->modifiers & kModSuper) != 0);
    CHECK(super_result->modifiers == gui_result->modifiers);
    CHECK(super_result->modifiers == meta_result->modifiers);
}

// ---------------------------------------------------------------------------
// parse_key_name: letters, numbers, function keys, special keys
// ---------------------------------------------------------------------------

TEST_CASE("keybinding parser: letter keys A-Z", "[keybinding][parser]")
{
    auto a = parse_gui_keybinding("copy", "Ctrl+A");
    auto z = parse_gui_keybinding("copy", "Ctrl+Z");
    REQUIRE(a.has_value());
    REQUIRE(z.has_value());
    CHECK(a->key == SDLK_A);
    CHECK(z->key == SDLK_Z);
}

TEST_CASE("keybinding parser: number keys 0-9", "[keybinding][parser]")
{
    auto zero = parse_gui_keybinding("font_reset", "Ctrl+0");
    auto nine = parse_gui_keybinding("font_reset", "Ctrl+9");
    REQUIRE(zero.has_value());
    REQUIRE(nine.has_value());
    CHECK(zero->key == SDLK_0);
    CHECK(nine->key == SDLK_9);
}

TEST_CASE("keybinding parser: function keys F1-F12", "[keybinding][parser]")
{
    auto f1 = parse_gui_keybinding("toggle_diagnostics", "F1");
    auto f12 = parse_gui_keybinding("toggle_diagnostics", "F12");
    REQUIRE(f1.has_value());
    REQUIRE(f12.has_value());
    CHECK(f1->key == SDLK_F1);
    CHECK(f12->key == SDLK_F12);
}

TEST_CASE("keybinding parser: special key Tab", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("toggle_diagnostics", "Ctrl+Tab");
    REQUIRE(result.has_value());
    CHECK(result->key == SDLK_TAB);
}

TEST_CASE("keybinding parser: special key Return", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("toggle_diagnostics", "Ctrl+Return");
    REQUIRE(result.has_value());
    CHECK(result->key == SDLK_RETURN);
}

TEST_CASE("keybinding parser: special key Space", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("toggle_diagnostics", "Ctrl+Space");
    REQUIRE(result.has_value());
    CHECK(result->key == SDLK_SPACE);
}

TEST_CASE("keybinding parser: special key Escape", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("toggle_diagnostics", "Escape");
    REQUIRE(result.has_value());
    CHECK(result->key == SDLK_ESCAPE);
}

TEST_CASE("keybinding parser: special key Backspace", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("toggle_diagnostics", "Ctrl+Backspace");
    REQUIRE(result.has_value());
    CHECK(result->key == SDLK_BACKSPACE);
}

TEST_CASE("keybinding parser: special key Delete", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("toggle_diagnostics", "Ctrl+Delete");
    REQUIRE(result.has_value());
    CHECK(result->key == SDLK_DELETE);
}

TEST_CASE("keybinding parser: special keys = and -", "[keybinding][parser]")
{
    auto eq = parse_gui_keybinding("font_increase", "Ctrl+=");
    auto minus = parse_gui_keybinding("font_decrease", "Ctrl+-");
    REQUIRE(eq.has_value());
    REQUIRE(minus.has_value());
    CHECK(eq->key == static_cast<int32_t>(SDLK_EQUALS));
    CHECK(minus->key == static_cast<int32_t>(SDLK_MINUS));
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST_CASE("keybinding parser: empty combo string returns nullopt", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("copy", "");
    CHECK_FALSE(result.has_value());
}

TEST_CASE("keybinding parser: unknown action name returns nullopt", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("not_a_real_action", "Ctrl+A");
    CHECK_FALSE(result.has_value());
}

TEST_CASE("keybinding parser: unknown key name returns nullopt", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("copy", "Ctrl+NotAKey");
    CHECK_FALSE(result.has_value());
}

TEST_CASE("keybinding parser: unknown modifier token returns nullopt", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("copy", "Hyper+A");
    CHECK_FALSE(result.has_value());
}

TEST_CASE("keybinding parser: whitespace around tokens is trimmed", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("copy", "  Ctrl + Shift + C  ");
    REQUIRE(result.has_value());
    CHECK(result->key == SDLK_C);
    CHECK((result->modifiers & kModCtrl) != 0);
    CHECK((result->modifiers & kModShift) != 0);
}

TEST_CASE("keybinding parser: only whitespace combo returns nullopt", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("copy", "   ");
    CHECK_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// format_gui_keybinding_combo roundtrip
// ---------------------------------------------------------------------------

TEST_CASE("keybinding parser: format roundtrip Ctrl+C", "[keybinding][parser]")
{
    auto parsed = parse_gui_keybinding("copy", "Ctrl+C");
    REQUIRE(parsed.has_value());
    std::string formatted = format_gui_keybinding_combo(parsed->key, parsed->modifiers);
    // The formatted string should contain both "Ctrl" and the key name.
    CHECK(formatted.find("Ctrl") != std::string::npos);
    // Re-parse the formatted string and verify same key/modifiers.
    auto reparsed = parse_gui_keybinding("copy", formatted);
    REQUIRE(reparsed.has_value());
    CHECK(reparsed->key == parsed->key);
    CHECK(reparsed->modifiers == parsed->modifiers);
}

TEST_CASE("keybinding parser: format roundtrip Ctrl+Shift+A", "[keybinding][parser]")
{
    auto parsed = parse_gui_keybinding("copy", "Ctrl+Shift+A");
    REQUIRE(parsed.has_value());
    std::string formatted = format_gui_keybinding_combo(parsed->key, parsed->modifiers);
    auto reparsed = parse_gui_keybinding("copy", formatted);
    REQUIRE(reparsed.has_value());
    CHECK(reparsed->key == parsed->key);
    CHECK(reparsed->modifiers == parsed->modifiers);
}

TEST_CASE("keybinding parser: format bare key no modifiers", "[keybinding][parser]")
{
    std::string formatted = format_gui_keybinding_combo(SDLK_F12, kModNone);
    CHECK(formatted.find('+') == std::string::npos);
    // Should just be the key name with no modifier prefix.
    auto reparsed = parse_gui_keybinding("toggle_diagnostics", formatted);
    REQUIRE(reparsed.has_value());
    CHECK(reparsed->key == SDLK_F12);
    CHECK(reparsed->modifiers == kModNone);
}

// ---------------------------------------------------------------------------
// Chord bindings (prefix, action)
// ---------------------------------------------------------------------------

TEST_CASE("keybinding parser: chord binding with comma separator", "[keybinding][parser]")
{
    auto result = parse_gui_keybinding("split_vertical", "Ctrl+S, |");
    REQUIRE(result.has_value());
    CHECK(result->prefix_key == SDLK_S);
    CHECK((result->prefix_modifiers & kModCtrl) != 0);
    CHECK(result->key == static_cast<int32_t>(SDLK_PIPE));
    CHECK(result->modifiers == kModNone);
}

// ---------------------------------------------------------------------------
// normalize_gui_modifiers: left/right collapsing
// (Tested indirectly through parse roundtrip — the function is file-local,
//  so we test via the public API that calls it.)
// ---------------------------------------------------------------------------

TEST_CASE("keybinding parser: left-only modifier bits are normalized", "[keybinding][parser]")
{
    // kModCtrl = 0x00C0 covers both left (0x0040) and right (0x0080).
    // If we set only the left bit, normalize should still produce kModCtrl.
    constexpr ModifierFlags kModLCtrl = 0x0040;
    constexpr ModifierFlags kModLShift = 0x0001;

    // Build a binding manually with left-only bits and check matching works.
    GuiKeybinding binding{ "copy", 0, kModNone, SDLK_C, kModCtrl };

    KeyEvent evt{};
    evt.keycode = SDLK_C;
    evt.mod = kModLCtrl; // only left ctrl bit
    evt.pressed = true;
    CHECK(gui_keybinding_matches(binding, evt));

    // Same for shift.
    GuiKeybinding shift_binding{ "copy", 0, kModNone, SDLK_C, kModShift };
    KeyEvent shift_evt{};
    shift_evt.keycode = SDLK_C;
    shift_evt.mod = kModLShift;
    shift_evt.pressed = true;
    CHECK(gui_keybinding_matches(shift_binding, shift_evt));
}

TEST_CASE("keybinding parser: right-only modifier bits are normalized", "[keybinding][parser]")
{
    constexpr ModifierFlags kModRCtrl = 0x0080;
    constexpr ModifierFlags kModRShift = 0x0002;

    GuiKeybinding binding{ "copy", 0, kModNone, SDLK_C, kModCtrl };

    KeyEvent evt{};
    evt.keycode = SDLK_C;
    evt.mod = kModRCtrl;
    evt.pressed = true;
    CHECK(gui_keybinding_matches(binding, evt));

    GuiKeybinding shift_binding{ "copy", 0, kModNone, SDLK_C, kModShift };
    KeyEvent shift_evt{};
    shift_evt.keycode = SDLK_C;
    shift_evt.mod = kModRShift;
    shift_evt.pressed = true;
    CHECK(gui_keybinding_matches(shift_binding, shift_evt));
}

TEST_CASE("keybinding parser: Return bindings also match keypad Enter", "[keybinding][parser]")
{
    GuiKeybinding binding{ "toggle_copy_mode", SDLK_S, kModCtrl, SDLK_RETURN, kModNone };

    KeyEvent evt{};
    evt.keycode = SDLK_KP_ENTER;
    evt.mod = kModNone;
    evt.pressed = true;
    CHECK(gui_keybinding_matches(binding, evt));
}
