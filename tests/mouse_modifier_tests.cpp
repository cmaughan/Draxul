// Tests for mouse event modifier translation in sdl_event_translator.cpp.
//
// SDL3 does not embed modifier state in mouse event structs (SDL_MouseButtonEvent,
// SDL_MouseWheelEvent, SDL_MouseMotionEvent all lack a .mod field). The translator
// therefore reads modifier state via SDL_GetModState(). These tests use
// SDL_SetModState() to control the global modifier state before each translation,
// verifying that the resulting draxul event carries the expected ModifierFlags.

#include "sdl_event_translator.h"

#include <draxul/input_types.h>

#include <SDL3/SDL.h>
#include <catch2/catch_all.hpp>

using namespace draxul;
using namespace draxul::sdl;

namespace
{

// RAII guard that saves and restores SDL modifier state around a test.
struct ModStateGuard
{
    SDL_Keymod saved;
    explicit ModStateGuard(SDL_Keymod mods)
        : saved(SDL_GetModState())
    {
        SDL_SetModState(mods);
    }
    ~ModStateGuard()
    {
        SDL_SetModState(saved);
    }
};

// Build a minimal SDL_EVENT_MOUSE_BUTTON_DOWN event at the given position.
SDL_Event make_button_event(Uint8 button, float x, float y, bool down = true)
{
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    ev.button.button = button;
    ev.button.down = down;
    ev.button.x = x;
    ev.button.y = y;
    return ev;
}

// Build a minimal SDL_EVENT_MOUSE_WHEEL event.
SDL_Event make_wheel_event(float dx, float dy, float mx, float my)
{
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_WHEEL;
    ev.wheel.x = dx;
    ev.wheel.y = dy;
    ev.wheel.mouse_x = mx;
    ev.wheel.mouse_y = my;
    return ev;
}

// Build a minimal SDL_EVENT_MOUSE_MOTION event.
SDL_Event make_motion_event(float x, float y)
{
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.x = x;
    ev.motion.y = y;
    return ev;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Button event tests
// ---------------------------------------------------------------------------

TEST_CASE("translate_mouse_button: no modifier — mod is kModNone", "[mouse_modifier]")
{
    ModStateGuard g(SDL_KMOD_NONE);
    SDL_Event ev = make_button_event(SDL_BUTTON_LEFT, 10.f, 20.f);
    auto result = translate_mouse_button(ev);
    REQUIRE(result.has_value());
    CHECK(result->mod == kModNone);
    CHECK(result->button == SDL_BUTTON_LEFT);
    CHECK(result->pressed == true);
    CHECK(result->x == 10);
    CHECK(result->y == 20);
}

TEST_CASE("translate_mouse_button: Ctrl+Shift — mod reflects both", "[mouse_modifier]")
{
    ModStateGuard g(static_cast<SDL_Keymod>(SDL_KMOD_CTRL | SDL_KMOD_SHIFT));
    SDL_Event ev = make_button_event(SDL_BUTTON_LEFT, 0.f, 0.f);
    auto result = translate_mouse_button(ev);
    REQUIRE(result.has_value());
    // Both kModCtrl and kModShift bits must be set.
    CHECK((result->mod & kModCtrl) != 0);
    CHECK((result->mod & kModShift) != 0);
}

TEST_CASE("translate_mouse_button: Alt only — kModAlt set, others clear", "[mouse_modifier]")
{
    ModStateGuard g(SDL_KMOD_ALT);
    SDL_Event ev = make_button_event(SDL_BUTTON_RIGHT, 5.f, 5.f);
    auto result = translate_mouse_button(ev);
    REQUIRE(result.has_value());
    CHECK((result->mod & kModAlt) != 0);
    CHECK((result->mod & kModCtrl) == 0);
    CHECK((result->mod & kModShift) == 0);
}

TEST_CASE("translate_mouse_button: release event — pressed is false", "[mouse_modifier]")
{
    ModStateGuard g(SDL_KMOD_NONE);
    SDL_Event ev = make_button_event(SDL_BUTTON_LEFT, 0.f, 0.f, false);
    auto result = translate_mouse_button(ev);
    REQUIRE(result.has_value());
    CHECK(result->pressed == false);
}

TEST_CASE("translate_mouse_button: wrong event type — returns nullopt", "[mouse_modifier]")
{
    ModStateGuard g(SDL_KMOD_NONE);
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_WHEEL; // not a button event
    CHECK(!translate_mouse_button(ev).has_value());
}

// ---------------------------------------------------------------------------
// Wheel event tests
// ---------------------------------------------------------------------------

TEST_CASE("translate_mouse_wheel: no modifier — mod is kModNone", "[mouse_modifier]")
{
    ModStateGuard g(SDL_KMOD_NONE);
    SDL_Event ev = make_wheel_event(0.f, 3.f, 100.f, 200.f);
    auto result = translate_mouse_wheel(ev);
    REQUIRE(result.has_value());
    CHECK(result->mod == kModNone);
    CHECK(result->dy == Catch::Approx(3.f));
    CHECK(result->x == 100);
    CHECK(result->y == 200);
}

TEST_CASE("translate_mouse_wheel: Alt — kModAlt set", "[mouse_modifier]")
{
    ModStateGuard g(SDL_KMOD_ALT);
    SDL_Event ev = make_wheel_event(0.f, -1.f, 0.f, 0.f);
    auto result = translate_mouse_wheel(ev);
    REQUIRE(result.has_value());
    CHECK((result->mod & kModAlt) != 0);
    CHECK((result->mod & kModCtrl) == 0);
}

TEST_CASE("translate_mouse_wheel: Ctrl+Alt — both bits set", "[mouse_modifier]")
{
    ModStateGuard g(static_cast<SDL_Keymod>(SDL_KMOD_CTRL | SDL_KMOD_ALT));
    SDL_Event ev = make_wheel_event(0.f, 1.f, 0.f, 0.f);
    auto result = translate_mouse_wheel(ev);
    REQUIRE(result.has_value());
    CHECK((result->mod & kModCtrl) != 0);
    CHECK((result->mod & kModAlt) != 0);
    CHECK((result->mod & kModShift) == 0);
}

TEST_CASE("translate_mouse_wheel: wrong event type — returns nullopt", "[mouse_modifier]")
{
    ModStateGuard g(SDL_KMOD_NONE);
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    CHECK(!translate_mouse_wheel(ev).has_value());
}

// ---------------------------------------------------------------------------
// Motion / drag event tests
// ---------------------------------------------------------------------------

TEST_CASE("translate_mouse_move: no modifier — mod is kModNone", "[mouse_modifier]")
{
    ModStateGuard g(SDL_KMOD_NONE);
    SDL_Event ev = make_motion_event(50.f, 60.f);
    auto result = translate_mouse_move(ev);
    REQUIRE(result.has_value());
    CHECK(result->mod == kModNone);
    CHECK(result->x == 50);
    CHECK(result->y == 60);
}

TEST_CASE("translate_mouse_move: Shift — kModShift set", "[mouse_modifier]")
{
    ModStateGuard g(SDL_KMOD_SHIFT);
    SDL_Event ev = make_motion_event(0.f, 0.f);
    auto result = translate_mouse_move(ev);
    REQUIRE(result.has_value());
    CHECK((result->mod & kModShift) != 0);
    CHECK((result->mod & kModCtrl) == 0);
    CHECK((result->mod & kModAlt) == 0);
}

TEST_CASE("translate_mouse_move: wrong event type — returns nullopt", "[mouse_modifier]")
{
    ModStateGuard g(SDL_KMOD_NONE);
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    CHECK(!translate_mouse_move(ev).has_value());
}
