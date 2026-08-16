#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_scancode.h>
#include <draxul/sdl_imgui_input.h>
#include <imgui.h>

// This exercises the single shared scancode table in draxul-imgui-core; core
// UI, the renderer backends, and every product plugin translate through it.
using namespace draxul;

TEST_CASE("ui_panel: sdl_scancode_to_imgui_key maps all expected navigation keys", "[ui_panel]")
{
    // Navigation keys must map to non-None ImGui keys
    const int navigation_scancodes[] = {
        SDL_SCANCODE_TAB,
        SDL_SCANCODE_LEFT,
        SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_UP,
        SDL_SCANCODE_DOWN,
        SDL_SCANCODE_PAGEUP,
        SDL_SCANCODE_PAGEDOWN,
        SDL_SCANCODE_HOME,
        SDL_SCANCODE_END,
        SDL_SCANCODE_INSERT,
        SDL_SCANCODE_DELETE,
        SDL_SCANCODE_BACKSPACE,
        SDL_SCANCODE_SPACE,
        SDL_SCANCODE_RETURN,
        SDL_SCANCODE_ESCAPE,
    };
    for (int sc : navigation_scancodes)
    {
        REQUIRE(sdl_scancode_to_imgui_key(sc) != ImGuiKey_None);
    }
}

TEST_CASE("ui_panel: sdl_scancode_to_imgui_key maps all letter keys A-Z", "[ui_panel]")
{
    const int letter_scancodes[] = {
        SDL_SCANCODE_A,
        SDL_SCANCODE_B,
        SDL_SCANCODE_C,
        SDL_SCANCODE_D,
        SDL_SCANCODE_E,
        SDL_SCANCODE_F,
        SDL_SCANCODE_G,
        SDL_SCANCODE_H,
        SDL_SCANCODE_I,
        SDL_SCANCODE_J,
        SDL_SCANCODE_K,
        SDL_SCANCODE_L,
        SDL_SCANCODE_M,
        SDL_SCANCODE_N,
        SDL_SCANCODE_O,
        SDL_SCANCODE_P,
        SDL_SCANCODE_Q,
        SDL_SCANCODE_R,
        SDL_SCANCODE_S,
        SDL_SCANCODE_T,
        SDL_SCANCODE_U,
        SDL_SCANCODE_V,
        SDL_SCANCODE_W,
        SDL_SCANCODE_X,
        SDL_SCANCODE_Y,
        SDL_SCANCODE_Z,
    };
    for (int sc : letter_scancodes)
    {
        REQUIRE(sdl_scancode_to_imgui_key(sc) != ImGuiKey_None);
    }
}

TEST_CASE("ui_panel: sdl_scancode_to_imgui_key maps all digit keys 0-9", "[ui_panel]")
{
    const int digit_scancodes[] = {
        SDL_SCANCODE_0,
        SDL_SCANCODE_1,
        SDL_SCANCODE_2,
        SDL_SCANCODE_3,
        SDL_SCANCODE_4,
        SDL_SCANCODE_5,
        SDL_SCANCODE_6,
        SDL_SCANCODE_7,
        SDL_SCANCODE_8,
        SDL_SCANCODE_9,
    };
    for (int sc : digit_scancodes)
    {
        REQUIRE(sdl_scancode_to_imgui_key(sc) != ImGuiKey_None);
    }
}

TEST_CASE("ui_panel: sdl_scancode_to_imgui_key maps F1-F12 function keys", "[ui_panel]")
{
    const int f_scancodes[] = {
        SDL_SCANCODE_F1,
        SDL_SCANCODE_F2,
        SDL_SCANCODE_F3,
        SDL_SCANCODE_F4,
        SDL_SCANCODE_F5,
        SDL_SCANCODE_F6,
        SDL_SCANCODE_F7,
        SDL_SCANCODE_F8,
        SDL_SCANCODE_F9,
        SDL_SCANCODE_F10,
        SDL_SCANCODE_F11,
        SDL_SCANCODE_F12,
    };
    for (int sc : f_scancodes)
    {
        REQUIRE(sdl_scancode_to_imgui_key(sc) != ImGuiKey_None);
    }
}

TEST_CASE("ui_panel: sdl_scancode_to_imgui_key maps keypad keys", "[ui_panel]")
{
    const int kp_scancodes[] = {
        SDL_SCANCODE_KP_0,
        SDL_SCANCODE_KP_1,
        SDL_SCANCODE_KP_2,
        SDL_SCANCODE_KP_3,
        SDL_SCANCODE_KP_4,
        SDL_SCANCODE_KP_5,
        SDL_SCANCODE_KP_6,
        SDL_SCANCODE_KP_7,
        SDL_SCANCODE_KP_8,
        SDL_SCANCODE_KP_9,
        SDL_SCANCODE_KP_ENTER,
        SDL_SCANCODE_KP_PERIOD,
        SDL_SCANCODE_KP_DIVIDE,
        SDL_SCANCODE_KP_MULTIPLY,
        SDL_SCANCODE_KP_MINUS,
        SDL_SCANCODE_KP_PLUS,
        SDL_SCANCODE_KP_EQUALS,
    };
    for (int sc : kp_scancodes)
    {
        REQUIRE(sdl_scancode_to_imgui_key(sc) != ImGuiKey_None);
    }
}

TEST_CASE("ui_panel: sdl_scancode_to_imgui_key maps modifier keys", "[ui_panel]")
{
    const int mod_scancodes[] = {
        SDL_SCANCODE_LCTRL,
        SDL_SCANCODE_RCTRL,
        SDL_SCANCODE_LSHIFT,
        SDL_SCANCODE_RSHIFT,
        SDL_SCANCODE_LALT,
        SDL_SCANCODE_RALT,
        SDL_SCANCODE_LGUI,
        SDL_SCANCODE_RGUI,
    };
    for (int sc : mod_scancodes)
    {
        REQUIRE(sdl_scancode_to_imgui_key(sc) != ImGuiKey_None);
    }
}

TEST_CASE("ui_panel: sdl_scancode_to_imgui_key maps lock and system keys", "[ui_panel]")
{
    // The keys product forks historically dropped (audit bug #1): losing any
    // of these here would silently kill them in every product ImGui panel.
    const int lock_scancodes[] = {
        SDL_SCANCODE_CAPSLOCK,
        SDL_SCANCODE_SCROLLLOCK,
        SDL_SCANCODE_NUMLOCKCLEAR,
        SDL_SCANCODE_PRINTSCREEN,
        SDL_SCANCODE_PAUSE,
    };
    for (int sc : lock_scancodes)
    {
        REQUIRE(sdl_scancode_to_imgui_key(sc) != ImGuiKey_None);
    }
}

TEST_CASE("ui_panel: sdl_scancode_to_imgui_key returns None for unmapped scancode", "[ui_panel]")
{
    // SDL_SCANCODE_UNKNOWN (0) should return ImGuiKey_None
    REQUIRE(sdl_scancode_to_imgui_key(SDL_SCANCODE_UNKNOWN) == ImGuiKey_None);
    // A scancode well outside the range should also return None
    REQUIRE(sdl_scancode_to_imgui_key(9999) == ImGuiKey_None);
}
