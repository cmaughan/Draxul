#include "satview_camera_key_state.h"

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>

using draxul::KeyEvent;
using draxul::kModCtrl;
using draxul::kModNone;
using draxul::satview::SatViewCameraKeyState;

namespace
{

KeyEvent key_event(int scancode, int keycode, bool pressed, draxul::ModifierFlags mod = kModNone)
{
    return KeyEvent{ scancode, keycode, mod, pressed };
}

} // namespace

TEST_CASE("SatView camera accepts MegaCity orbit key alternatives", "[satview][input]")
{
    SatViewCameraKeyState input;

    CHECK(input.on_key(key_event(SDL_SCANCODE_A, SDLK_A, true)));
    CHECK(input.movement().orbit.x == 1.0f);
    CHECK(input.on_key(key_event(SDL_SCANCODE_Q, SDLK_Q, true)));
    CHECK(input.on_key(key_event(SDL_SCANCODE_A, SDLK_A, false)));
    CHECK(input.movement().orbit.x == 1.0f);
    CHECK(input.on_key(key_event(SDL_SCANCODE_Q, SDLK_Q, false)));
    CHECK_FALSE(input.movement_active());

    CHECK(input.on_key(key_event(SDL_SCANCODE_W, SDLK_W, true)));
    CHECK(input.movement().orbit.y == 1.0f);
    CHECK(input.on_key(key_event(SDL_SCANCODE_W, SDLK_W, false)));
    CHECK(input.on_key(key_event(SDL_SCANCODE_G, SDLK_G, true)));
    CHECK(input.movement().orbit.y == -1.0f);
}

TEST_CASE("SatView camera uses R and F for dolly while reserving Ctrl R", "[satview][input]")
{
    SatViewCameraKeyState input;

    CHECK(input.on_key(key_event(SDL_SCANCODE_R, SDLK_R, true)));
    CHECK(input.movement().zoom == -1.0f);
    CHECK(input.on_key(key_event(SDL_SCANCODE_R, SDLK_R, false)));
    CHECK_FALSE(input.movement_active());

    CHECK_FALSE(input.on_key(key_event(SDL_SCANCODE_R, SDLK_R, true, kModCtrl)));
    CHECK_FALSE(input.movement_active());
    CHECK(input.on_key(key_event(SDL_SCANCODE_F, SDLK_F, true)));
    CHECK(input.movement().zoom == 1.0f);
    input.reset();
    CHECK_FALSE(input.movement_active());
}
