#pragma once

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>
#include <draxul/events.h>
#include <glm/vec2.hpp>

namespace draxul::satview
{

struct SatViewCameraMovement
{
    glm::vec2 orbit{ 0.0f };
    float zoom = 0.0f;
};

class SatViewCameraKeyState
{
public:
    void reset()
    {
        horizontal_left_ = false;
        horizontal_right_ = false;
        orbit_left_ = false;
        orbit_right_ = false;
        vertical_up_ = false;
        vertical_down_ = false;
        pitch_up_ = false;
        pitch_down_ = false;
        zoom_in_ = false;
        zoom_out_ = false;
    }

    bool on_key(const KeyEvent& event)
    {
        if (matches(event, SDL_SCANCODE_LEFT, SDLK_LEFT) || matches(event, SDL_SCANCODE_A, SDLK_A))
            return update(horizontal_left_, event.pressed);
        if (matches(event, SDL_SCANCODE_RIGHT, SDLK_RIGHT) || matches(event, SDL_SCANCODE_D, SDLK_D))
            return update(horizontal_right_, event.pressed);
        if (matches(event, SDL_SCANCODE_Q, SDLK_Q))
            return update(orbit_left_, event.pressed);
        if (matches(event, SDL_SCANCODE_E, SDLK_E))
            return update(orbit_right_, event.pressed);
        if (matches(event, SDL_SCANCODE_UP, SDLK_UP) || matches(event, SDL_SCANCODE_W, SDLK_W))
            return update(vertical_up_, event.pressed);
        if (matches(event, SDL_SCANCODE_DOWN, SDLK_DOWN) || matches(event, SDL_SCANCODE_S, SDLK_S))
            return update(vertical_down_, event.pressed);
        if (matches(event, SDL_SCANCODE_T, SDLK_T))
            return update(pitch_up_, event.pressed);
        if (matches(event, SDL_SCANCODE_G, SDLK_G))
            return update(pitch_down_, event.pressed);
        if (matches(event, SDL_SCANCODE_R, SDLK_R))
        {
            if (event.pressed && (event.mod & kModCtrl) != 0)
                return false;
            return update(zoom_in_, event.pressed);
        }
        if (matches(event, SDL_SCANCODE_F, SDLK_F))
            return update(zoom_out_, event.pressed);
        return false;
    }

    [[nodiscard]] bool movement_active() const
    {
        return horizontal_left_ || horizontal_right_ || orbit_left_ || orbit_right_
            || vertical_up_ || vertical_down_ || pitch_up_ || pitch_down_
            || zoom_in_ || zoom_out_;
    }

    [[nodiscard]] SatViewCameraMovement movement() const
    {
        SatViewCameraMovement result;
        if (horizontal_left_ || orbit_left_)
            result.orbit.x += 1.0f;
        if (horizontal_right_ || orbit_right_)
            result.orbit.x -= 1.0f;
        if (vertical_up_ || pitch_up_)
            result.orbit.y += 1.0f;
        if (vertical_down_ || pitch_down_)
            result.orbit.y -= 1.0f;
        if (zoom_in_)
            result.zoom -= 1.0f;
        if (zoom_out_)
            result.zoom += 1.0f;
        return result;
    }

private:
    static bool matches(const KeyEvent& event, int scancode, int keycode)
    {
        return event.scancode == scancode || event.keycode == keycode;
    }

    static bool update(bool& value, bool pressed)
    {
        const bool changed = value != pressed;
        value = pressed;
        return changed;
    }

    bool horizontal_left_ = false;
    bool horizontal_right_ = false;
    bool orbit_left_ = false;
    bool orbit_right_ = false;
    bool vertical_up_ = false;
    bool vertical_down_ = false;
    bool pitch_up_ = false;
    bool pitch_down_ = false;
    bool zoom_in_ = false;
    bool zoom_out_ = false;
};

} // namespace draxul::satview
