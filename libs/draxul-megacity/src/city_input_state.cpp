#include "city_input_state.h"
#include "isometric_camera.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <glm/geometric.hpp>

namespace draxul
{

namespace
{

constexpr float kOrbitSpeedRadiansPerSecond = 1.8f;
constexpr float kOrbitDragReferencePixelsPerSecond = 240.0f;
constexpr float kOrbitDragRadiansPerPixel = kOrbitSpeedRadiansPerSecond / kOrbitDragReferencePixelsPerSecond;
constexpr float kDragCatchUpRatePerSecond = 30.0f;
constexpr float kDragPanSettleEpsilon = 1e-4f;
constexpr float kDragOrbitSettleEpsilon = 1e-4f;

bool is_left_arrow(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_LEFT || event.keycode == SDLK_LEFT
        || event.scancode == SDL_SCANCODE_A || event.keycode == SDLK_A;
}

bool is_right_arrow(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_RIGHT || event.keycode == SDLK_RIGHT
        || event.scancode == SDL_SCANCODE_D || event.keycode == SDLK_D;
}

bool is_up_arrow(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_UP || event.keycode == SDLK_UP
        || event.scancode == SDL_SCANCODE_W || event.keycode == SDLK_W;
}

bool is_down_arrow(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_DOWN || event.keycode == SDLK_DOWN
        || event.scancode == SDL_SCANCODE_S || event.keycode == SDLK_S;
}

bool is_orbit_left_key(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_Q || event.keycode == SDLK_Q;
}

bool is_orbit_right_key(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_E || event.keycode == SDLK_E;
}

bool is_zoom_in_key(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_R || event.keycode == SDLK_R;
}

bool is_zoom_out_key(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_F || event.keycode == SDLK_F;
}

bool is_pitch_up_key(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_T || event.keycode == SDLK_T;
}

bool is_pitch_down_key(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_G || event.keycode == SDLK_G;
}

float drag_catch_up_alpha(float dt)
{
    if (dt <= 0.0f)
        return 0.0f;
    return 1.0f - std::exp(-kDragCatchUpRatePerSecond * dt);
}

void clamp_small_pan(glm::vec2& pan)
{
    if (glm::dot(pan, pan) <= kDragPanSettleEpsilon * kDragPanSettleEpsilon)
        pan = glm::vec2(0.0f);
}

void clamp_small_orbit(float& orbit)
{
    if (std::abs(orbit) <= kDragOrbitSettleEpsilon)
        orbit = 0.0f;
}

} // namespace

void CityInputState::reset_keys()
{
    move_left_ = false;
    move_right_ = false;
    move_up_ = false;
    move_down_ = false;
    orbit_left_ = false;
    orbit_right_ = false;
    zoom_in_ = false;
    zoom_out_ = false;
    pitch_up_ = false;
    pitch_down_ = false;
}

bool CityInputState::on_key(const KeyEvent& event)
{
    bool changed = false;
    if (is_left_arrow(event))
    {
        changed = move_left_ != event.pressed;
        move_left_ = event.pressed;
    }
    else if (is_right_arrow(event))
    {
        changed = move_right_ != event.pressed;
        move_right_ = event.pressed;
    }
    else if (is_up_arrow(event))
    {
        changed = move_up_ != event.pressed;
        move_up_ = event.pressed;
    }
    else if (is_down_arrow(event))
    {
        changed = move_down_ != event.pressed;
        move_down_ = event.pressed;
    }
    else if (is_orbit_left_key(event))
    {
        changed = orbit_left_ != event.pressed;
        orbit_left_ = event.pressed;
    }
    else if (is_orbit_right_key(event))
    {
        changed = orbit_right_ != event.pressed;
        orbit_right_ = event.pressed;
    }
    else if (is_zoom_in_key(event))
    {
        changed = zoom_in_ != event.pressed;
        zoom_in_ = event.pressed;
    }
    else if (is_zoom_out_key(event))
    {
        changed = zoom_out_ != event.pressed;
        zoom_out_ = event.pressed;
    }
    else if (is_pitch_up_key(event))
    {
        changed = pitch_up_ != event.pressed;
        pitch_up_ = event.pressed;
    }
    else if (is_pitch_down_key(event))
    {
        changed = pitch_down_ != event.pressed;
        pitch_down_ = event.pressed;
    }

    return changed;
}

void CityInputState::on_mouse_button(const MouseButtonEvent& event)
{
    if (event.button != SDL_BUTTON_LEFT)
        return;

    if (event.pressed)
    {
        dragging_scene_ = true;
        last_drag_pos_ = event.pos;
        press_pos_ = event.pos;
        was_dragged_ = false;
    }
    else
    {
        dragging_scene_ = false;
        if (!was_dragged_)
            pending_click_ = event.pos;
    }
}

bool CityInputState::on_mouse_move(const MouseMoveEvent& event, IsometricCamera& camera)
{
    if (!dragging_scene_)
        return false;

    if (SDL_WasInit(SDL_INIT_VIDEO) != 0
        && (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) == 0)
    {
        dragging_scene_ = false;
        return false;
    }

    glm::vec2 pixel_delta = event.delta;
    if (glm::dot(pixel_delta, pixel_delta) <= 0.0f)
    {
        const glm::ivec2 fallback_delta = event.pos - last_drag_pos_;
        pixel_delta = glm::vec2(static_cast<float>(fallback_delta.x), static_cast<float>(fallback_delta.y));
    }
    last_drag_pos_ = event.pos;
    if (glm::dot(pixel_delta, pixel_delta) <= 0.0f)
        return false;

    // Mark as drag once movement exceeds threshold
    const glm::ivec2 total_delta = event.pos - press_pos_;
    if (std::abs(total_delta.x) > 4 || std::abs(total_delta.y) > 4)
        was_dragged_ = true;

    if ((event.mod & kModAlt) != 0)
    {
        if (pixel_delta.x != 0.0f)
        {
            pending_drag_orbit_ += -pixel_delta.x * kOrbitDragRadiansPerPixel;
            return true;
        }
        return false;
    }

    const glm::vec2 pan = camera.pan_delta_for_screen_drag(pixel_delta);
    if (glm::dot(pan, pan) <= 0.0f)
        return false;

    pending_drag_pan_ += pan;
    return true;
}

bool CityInputState::movement_active() const
{
    return move_left_ || move_right_ || move_up_ || move_down_ || orbit_left_ || orbit_right_
        || zoom_in_ || zoom_out_ || pitch_up_ || pitch_down_;
}

bool CityInputState::drag_smoothing_active() const
{
    return glm::dot(pending_drag_pan_, pending_drag_pan_) > kDragPanSettleEpsilon * kDragPanSettleEpsilon
        || std::abs(pending_drag_orbit_) > kDragOrbitSettleEpsilon;
}

CameraMovement CityInputState::movement() const
{
    CameraMovement m;
    if (move_left_)
        m.pan_input.x -= 1.0f;
    if (move_right_)
        m.pan_input.x += 1.0f;
    if (move_up_)
        m.pan_input.y += 1.0f;
    if (move_down_)
        m.pan_input.y -= 1.0f;

    if (orbit_left_)
        m.orbit += 1.0f;
    if (orbit_right_)
        m.orbit -= 1.0f;

    if (zoom_in_)
        m.zoom -= 1.0f;
    if (zoom_out_)
        m.zoom += 1.0f;

    if (pitch_up_)
        m.pitch += 1.0f;
    if (pitch_down_)
        m.pitch -= 1.0f;

    return m;
}

bool CityInputState::apply_drag_smoothing(float dt, IsometricCamera& camera)
{
    if (!drag_smoothing_active())
        return false;

    const float alpha = drag_catch_up_alpha(dt);
    if (alpha <= 0.0f)
        return false;

    bool changed = false;
    if (glm::dot(pending_drag_pan_, pending_drag_pan_) > 0.0f)
    {
        glm::vec2 applied_pan = pending_drag_pan_ * alpha;
        camera.translate_target(applied_pan.x, applied_pan.y);
        pending_drag_pan_ -= applied_pan;
        clamp_small_pan(pending_drag_pan_);
        changed = true;
    }

    if (pending_drag_orbit_ != 0.0f)
    {
        const float applied_orbit = pending_drag_orbit_ * alpha;
        camera.orbit_target(applied_orbit);
        pending_drag_orbit_ -= applied_orbit;
        clamp_small_orbit(pending_drag_orbit_);
        changed = true;
    }

    return changed;
}

std::optional<glm::ivec2> CityInputState::consume_click()
{
    auto click = pending_click_;
    pending_click_.reset();
    return click;
}

} // namespace draxul
