// Shared camera-input leaf (Draxul::PluginSupport::CameraInput).
//
// The point of these cases is the DRIFT RESOLUTION recorded in
// draxul/camera_input.h: MegaCity bound the arrow/WASD groups to pan with a
// separate pitch axis, SatView folded the same groups into vertical/horizontal
// orbit. One table now serves both, with the axis choice made per product by
// OrbitKeyBindings, and neither product's behaviour changed. These cases pin
// both bindings so a future "simplification" of the table cannot quietly change
// one product's controls.

#include <draxul/camera_input.h>

#include <SDL3/SDL.h>
#include <catch2/catch_all.hpp>

using draxul::KeyEvent;
using draxul::kModCtrl;
using draxul::kModNone;
using draxul::kModShift;
using draxul::MouseButtonEvent;
using draxul::MouseMoveEvent;
using draxul::camera_input::DragSmoother;
using draxul::camera_input::DragSmootherConfig;
using draxul::camera_input::OrbitKeyBindings;
using draxul::camera_input::OrbitKeyState;

namespace
{

KeyEvent key_event(int scancode, int keycode, bool pressed,
    draxul::ModifierFlags mod = kModNone)
{
    return KeyEvent{ scancode, keycode, mod, pressed };
}

// MegaCity: arrows/WASD pan, Q/E orbit, T/G pitch as its own axis.
constexpr OrbitKeyBindings kPanBindings{
    .horizontal_arrows_orbit = false,
    .vertical_arrows_orbit = false,
    .pitch_folds_into_orbit = false,
    .zoom_in_guard_modifiers = draxul::kModCtrl,
};

// SatView: every directional group feeds the orbit axes.
constexpr OrbitKeyBindings kOrbitBindings{
    .horizontal_arrows_orbit = true,
    .vertical_arrows_orbit = true,
    .pitch_folds_into_orbit = true,
    .zoom_in_guard_modifiers = draxul::kModCtrl,
};

} // namespace

TEST_CASE("pan bindings keep arrows on pan and pitch on its own axis", "[camera_input]")
{
    OrbitKeyState input(kPanBindings);

    REQUIRE(input.on_key(key_event(SDL_SCANCODE_W, SDLK_W, true)));
    CHECK(input.movement().pan.y == 1.0f);
    CHECK(input.movement().orbit.y == 0.0f);
    REQUIRE(input.on_key(key_event(SDL_SCANCODE_W, SDLK_W, false)));

    REQUIRE(input.on_key(key_event(SDL_SCANCODE_A, SDLK_A, true)));
    CHECK(input.movement().pan.x == -1.0f);
    CHECK(input.movement().orbit.x == 0.0f);
    REQUIRE(input.on_key(key_event(SDL_SCANCODE_A, SDLK_A, false)));

    // Q/E remain the orbit axis even when the arrows pan.
    REQUIRE(input.on_key(key_event(SDL_SCANCODE_Q, SDLK_Q, true)));
    CHECK(input.movement().orbit.x == 1.0f);
    CHECK(input.movement().pan.x == 0.0f);
    REQUIRE(input.on_key(key_event(SDL_SCANCODE_Q, SDLK_Q, false)));

    REQUIRE(input.on_key(key_event(SDL_SCANCODE_T, SDLK_T, true)));
    CHECK(input.movement().pitch == 1.0f);
    CHECK(input.movement().orbit.y == 0.0f);
    REQUIRE(input.on_key(key_event(SDL_SCANCODE_G, SDLK_G, true)));
    // Both pitch keys held cancel out, as they did before.
    CHECK(input.movement().pitch == 0.0f);
}

TEST_CASE("orbit bindings fold the arrow groups into orbit", "[camera_input]")
{
    OrbitKeyState input(kOrbitBindings);

    REQUIRE(input.on_key(key_event(SDL_SCANCODE_A, SDLK_A, true)));
    CHECK(input.movement().orbit.x == 1.0f);
    CHECK(input.movement().pan.x == 0.0f);

    REQUIRE(input.on_key(key_event(SDL_SCANCODE_W, SDLK_W, true)));
    CHECK(input.movement().orbit.y == 1.0f);
    CHECK(input.movement().pan.y == 0.0f);
    input.reset();

    REQUIRE(input.on_key(key_event(SDL_SCANCODE_G, SDLK_G, true)));
    CHECK(input.movement().orbit.y == -1.0f);
    // Pitch is still reported on its own axis for products that want both.
    CHECK(input.movement().pitch == -1.0f);
}

TEST_CASE("folded key groups combine with OR, never by summing", "[camera_input]")
{
    // The regression this guards: a fold implemented as `orbit += pitch` would
    // make A+Q (or W+T) orbit at DOUBLE speed, silently changing SatView's feel
    // because its rate constants assume a unit input.
    OrbitKeyState input(kOrbitBindings);

    REQUIRE(input.on_key(key_event(SDL_SCANCODE_A, SDLK_A, true)));
    REQUIRE(input.on_key(key_event(SDL_SCANCODE_Q, SDLK_Q, true)));
    CHECK(input.movement().orbit.x == 1.0f);

    REQUIRE(input.on_key(key_event(SDL_SCANCODE_W, SDLK_W, true)));
    REQUIRE(input.on_key(key_event(SDL_SCANCODE_T, SDLK_T, true)));
    CHECK(input.movement().orbit.y == 1.0f);

    // Releasing one of a folded pair leaves the axis fully engaged.
    REQUIRE(input.on_key(key_event(SDL_SCANCODE_A, SDLK_A, false)));
    CHECK(input.movement().orbit.x == 1.0f);
    REQUIRE(input.on_key(key_event(SDL_SCANCODE_T, SDLK_T, false)));
    CHECK(input.movement().orbit.y == 1.0f);
}

TEST_CASE("zoom-in guard keeps the host accelerator", "[camera_input]")
{
    // SatView guarded Ctrl+R so the host reload shortcut was not eaten; MegaCity
    // had no guard. Guarding is the shared behaviour now, configurably.
    OrbitKeyState guarded(kOrbitBindings);
    CHECK_FALSE(guarded.on_key(key_event(SDL_SCANCODE_R, SDLK_R, true, kModCtrl)));
    CHECK_FALSE(guarded.movement_active());
    CHECK(guarded.on_key(key_event(SDL_SCANCODE_R, SDLK_R, true)));
    CHECK(guarded.movement().zoom == -1.0f);
    // A non-guarded modifier still zooms.
    guarded.reset();
    CHECK(guarded.on_key(key_event(SDL_SCANCODE_R, SDLK_R, true, kModShift)));
    CHECK(guarded.movement().zoom == -1.0f);

    OrbitKeyBindings unguarded = kOrbitBindings;
    unguarded.zoom_in_guard_modifiers = 0;
    OrbitKeyState open(unguarded);
    CHECK(open.on_key(key_event(SDL_SCANCODE_R, SDLK_R, true, kModCtrl)));
    CHECK(open.movement().zoom == -1.0f);
}

TEST_CASE("a guarded zoom-in release still clears the latch", "[camera_input]")
{
    // Guarding only the PRESS matters: swallowing the release too would leave R
    // latched down forever if the user pressed R and then tapped Ctrl.
    OrbitKeyState input(kOrbitBindings);
    REQUIRE(input.on_key(key_event(SDL_SCANCODE_R, SDLK_R, true)));
    REQUIRE(input.movement_active());
    CHECK(input.on_key(key_event(SDL_SCANCODE_R, SDLK_R, false, kModCtrl)));
    CHECK_FALSE(input.movement_active());
}

TEST_CASE("orbit key state reports only changes", "[camera_input]")
{
    OrbitKeyState input(kPanBindings);
    CHECK(input.on_key(key_event(SDL_SCANCODE_LEFT, SDLK_LEFT, true)));
    // A repeat of an already-latched key is not a change.
    CHECK_FALSE(input.on_key(key_event(SDL_SCANCODE_LEFT, SDLK_LEFT, true)));
    // An unbound key is never consumed.
    CHECK_FALSE(input.on_key(key_event(SDL_SCANCODE_Z, SDLK_Z, true)));
    CHECK(input.movement_active());
    input.reset();
    CHECK_FALSE(input.movement_active());
}

namespace
{

MouseButtonEvent button_event(int x, int y, bool pressed, int button = SDL_BUTTON_LEFT)
{
    MouseButtonEvent event;
    event.button = button;
    event.pos = { x, y };
    event.pressed = pressed;
    return event;
}

MouseMoveEvent move_event(int x, int y, float dx, float dy)
{
    MouseMoveEvent event;
    event.pos = { x, y };
    event.delta = { dx, dy };
    return event;
}

} // namespace

TEST_CASE("drag smoother distinguishes a click from a drag", "[camera_input]")
{
    DragSmoother smoother;
    smoother.on_mouse_button(button_event(100, 100, true), SDL_BUTTON_LEFT);
    CHECK(smoother.dragging());
    smoother.on_mouse_button(button_event(100, 100, false), SDL_BUTTON_LEFT);
    CHECK_FALSE(smoother.dragging());
    const auto click = smoother.consume_click();
    REQUIRE(click.has_value());
    CHECK(click->x == 100);
    // Consuming is one-shot.
    CHECK_FALSE(smoother.consume_click().has_value());

    // Travel past the threshold suppresses the click.
    smoother.on_mouse_button(button_event(100, 100, true), SDL_BUTTON_LEFT);
    CHECK(smoother.on_mouse_move(move_event(140, 100, 40.0f, 0.0f)).has_value());
    smoother.on_mouse_button(button_event(140, 100, false), SDL_BUTTON_LEFT);
    CHECK_FALSE(smoother.consume_click().has_value());
}

TEST_CASE("drag smoother detects a double click", "[camera_input]")
{
    DragSmoother smoother;
    smoother.on_mouse_button(button_event(50, 50, true), SDL_BUTTON_LEFT);
    smoother.on_mouse_button(button_event(50, 50, false), SDL_BUTTON_LEFT);
    REQUIRE(smoother.consume_click().has_value());

    smoother.on_mouse_button(button_event(51, 50, true), SDL_BUTTON_LEFT);
    smoother.on_mouse_button(button_event(51, 50, false), SDL_BUTTON_LEFT);
    const auto double_click = smoother.consume_double_click();
    REQUIRE(double_click.has_value());
    CHECK(double_click->x == 51);
    // The second press of a double click does not also report a single click.
    CHECK_FALSE(smoother.consume_click().has_value());
}

TEST_CASE("drag smoother ignores non-left buttons and moves outside a drag", "[camera_input]")
{
    DragSmoother smoother;
    smoother.on_mouse_button(button_event(10, 10, true, SDL_BUTTON_RIGHT), SDL_BUTTON_LEFT);
    CHECK_FALSE(smoother.dragging());
    CHECK_FALSE(smoother.on_mouse_move(move_event(20, 20, 10.0f, 10.0f)).has_value());
}

TEST_CASE("drag smoother falls back to the position delta", "[camera_input]")
{
    // Platforms occasionally report a zero delta with a moved position.
    DragSmoother smoother;
    smoother.on_mouse_button(button_event(0, 0, true), SDL_BUTTON_LEFT);
    const auto delta = smoother.on_mouse_move(move_event(7, 3, 0.0f, 0.0f));
    REQUIRE(delta.has_value());
    CHECK(delta->x == 7.0f);
    CHECK(delta->y == 3.0f);
    // No travel at all yields nothing.
    CHECK_FALSE(smoother.on_mouse_move(move_event(7, 3, 0.0f, 0.0f)).has_value());
}

TEST_CASE("drag smoother bleeds out accumulated motion and settles", "[camera_input]")
{
    DragSmoother smoother;
    smoother.add_pan({ 10.0f, 0.0f });
    smoother.add_orbit(2.0f);
    REQUIRE(smoother.smoothing_active());

    // A zero/negative tick applies nothing.
    CHECK(smoother.consume_step(0.0f).empty());

    const auto first = smoother.consume_step(1.0f / 60.0f);
    CHECK(first.pan.x > 0.0f);
    CHECK(first.pan.x < 10.0f); // eased, not applied all at once
    CHECK(first.orbit > 0.0f);

    // Repeated ticks converge and then stop reporting work.
    for (int i = 0; i < 200 && smoother.smoothing_active(); ++i)
        smoother.consume_step(1.0f / 60.0f);
    CHECK_FALSE(smoother.smoothing_active());
    CHECK(smoother.consume_step(1.0f / 60.0f).empty());
}
