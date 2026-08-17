#include <draxul/camera_input.h>

#include <cmath>

namespace draxul::camera_input
{

void OrbitKeyState::reset()
{
    left_ = false;
    right_ = false;
    up_ = false;
    down_ = false;
    orbit_left_ = false;
    orbit_right_ = false;
    zoom_in_ = false;
    zoom_out_ = false;
    pitch_up_ = false;
    pitch_down_ = false;
}

bool OrbitKeyState::on_key(const KeyEvent& event)
{
    if (matches(event, SDL_SCANCODE_LEFT, SDLK_LEFT) || matches(event, SDL_SCANCODE_A, SDLK_A))
        return update(left_, event.pressed);
    if (matches(event, SDL_SCANCODE_RIGHT, SDLK_RIGHT) || matches(event, SDL_SCANCODE_D, SDLK_D))
        return update(right_, event.pressed);
    if (matches(event, SDL_SCANCODE_UP, SDLK_UP) || matches(event, SDL_SCANCODE_W, SDLK_W))
        return update(up_, event.pressed);
    if (matches(event, SDL_SCANCODE_DOWN, SDLK_DOWN) || matches(event, SDL_SCANCODE_S, SDLK_S))
        return update(down_, event.pressed);
    if (matches(event, SDL_SCANCODE_Q, SDLK_Q))
        return update(orbit_left_, event.pressed);
    if (matches(event, SDL_SCANCODE_E, SDLK_E))
        return update(orbit_right_, event.pressed);
    if (matches(event, SDL_SCANCODE_T, SDLK_T))
        return update(pitch_up_, event.pressed);
    if (matches(event, SDL_SCANCODE_G, SDLK_G))
        return update(pitch_down_, event.pressed);
    if (matches(event, SDL_SCANCODE_R, SDLK_R))
    {
        // Guarded so a host accelerator such as Ctrl+R (reload) is not consumed
        // as a camera zoom. Releases still clear the latch, otherwise a key held
        // through a modifier change would stay stuck down.
        if (event.pressed && bindings_.zoom_in_guard_modifiers != 0
            && (event.mod & bindings_.zoom_in_guard_modifiers) != 0)
            return false;
        return update(zoom_in_, event.pressed);
    }
    if (matches(event, SDL_SCANCODE_F, SDLK_F))
        return update(zoom_out_, event.pressed);
    return false;
}

bool OrbitKeyState::movement_active() const
{
    return left_ || right_ || up_ || down_ || orbit_left_ || orbit_right_
        || zoom_in_ || zoom_out_ || pitch_up_ || pitch_down_;
}

OrbitMovement OrbitKeyState::movement() const
{
    OrbitMovement movement;

    // Folded groups combine with OR, never by summing: holding A and Q together
    // (or W and T) must orbit at the SAME rate as either key alone, which is
    // what SatView's table did and what its tuned rate constants assume.
    const bool orbit_left = orbit_left_ || (bindings_.horizontal_arrows_orbit && left_);
    const bool orbit_right = orbit_right_ || (bindings_.horizontal_arrows_orbit && right_);
    if (orbit_left)
        movement.orbit.x += 1.0f;
    if (orbit_right)
        movement.orbit.x -= 1.0f;

    const bool orbit_up = (bindings_.vertical_arrows_orbit && up_)
        || (bindings_.pitch_folds_into_orbit && pitch_up_);
    const bool orbit_down = (bindings_.vertical_arrows_orbit && down_)
        || (bindings_.pitch_folds_into_orbit && pitch_down_);
    if (orbit_up)
        movement.orbit.y += 1.0f;
    if (orbit_down)
        movement.orbit.y -= 1.0f;

    // Whatever is not folded into orbit stays on the pan axis.
    if (!bindings_.horizontal_arrows_orbit)
    {
        if (left_)
            movement.pan.x -= 1.0f;
        if (right_)
            movement.pan.x += 1.0f;
    }
    if (!bindings_.vertical_arrows_orbit)
    {
        if (up_)
            movement.pan.y += 1.0f;
        if (down_)
            movement.pan.y -= 1.0f;
    }

    if (zoom_in_)
        movement.zoom -= 1.0f;
    if (zoom_out_)
        movement.zoom += 1.0f;

    // Pitch is always reported from its own keys, folded or not.
    if (pitch_up_)
        movement.pitch += 1.0f;
    if (pitch_down_)
        movement.pitch -= 1.0f;

    return movement;
}

void DragSmoother::on_mouse_button(const MouseButtonEvent& event, int left_button)
{
    if (event.button != left_button)
        return;

    if (event.pressed)
    {
        dragging_ = true;
        last_drag_pos_ = event.pos;
        press_pos_ = event.pos;
        was_dragged_ = false;
        return;
    }

    dragging_ = false;
    if (was_dragged_)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (has_last_click_)
    {
        const float elapsed_ms
            = std::chrono::duration<float, std::milli>(now - last_click_time_).count();
        const glm::ivec2 delta = event.pos - last_click_pos_;
        const float distance = std::sqrt(
            static_cast<float>(delta.x * delta.x + delta.y * delta.y));
        if (elapsed_ms <= config_.double_click_max_time_ms
            && distance <= config_.double_click_max_distance_px)
        {
            pending_double_click_ = event.pos;
            has_last_click_ = false;
            return;
        }
    }

    pending_click_ = event.pos;
    last_click_time_ = now;
    last_click_pos_ = event.pos;
    has_last_click_ = true;
}

std::optional<glm::vec2> DragSmoother::on_mouse_move(const MouseMoveEvent& event)
{
    if (!dragging_)
        return std::nullopt;

    glm::vec2 pixel_delta = event.delta;
    if (glm::dot(pixel_delta, pixel_delta) <= 0.0f)
    {
        const glm::ivec2 fallback = event.pos - last_drag_pos_;
        pixel_delta = glm::vec2(static_cast<float>(fallback.x), static_cast<float>(fallback.y));
    }
    last_drag_pos_ = event.pos;
    if (glm::dot(pixel_delta, pixel_delta) <= 0.0f)
        return std::nullopt;

    const glm::ivec2 total = event.pos - press_pos_;
    if (std::abs(total.x) > config_.drag_threshold_px
        || std::abs(total.y) > config_.drag_threshold_px)
        was_dragged_ = true;

    return pixel_delta;
}

bool DragSmoother::smoothing_active() const
{
    return glm::dot(pending_pan_, pending_pan_)
        > config_.pan_settle_epsilon * config_.pan_settle_epsilon
        || std::abs(pending_orbit_) > config_.orbit_settle_epsilon;
}

DragStep DragSmoother::consume_step(float dt)
{
    DragStep step;
    if (!smoothing_active() || dt <= 0.0f)
        return step;

    const float alpha = 1.0f - std::exp(-config_.catch_up_rate_per_second * dt);
    if (alpha <= 0.0f)
        return step;

    if (glm::dot(pending_pan_, pending_pan_) > 0.0f)
    {
        step.pan = pending_pan_ * alpha;
        pending_pan_ -= step.pan;
        if (glm::dot(pending_pan_, pending_pan_)
            <= config_.pan_settle_epsilon * config_.pan_settle_epsilon)
            pending_pan_ = glm::vec2(0.0f);
    }

    if (pending_orbit_ != 0.0f)
    {
        step.orbit = pending_orbit_ * alpha;
        pending_orbit_ -= step.orbit;
        if (std::abs(pending_orbit_) <= config_.orbit_settle_epsilon)
            pending_orbit_ = 0.0f;
    }

    return step;
}

std::optional<glm::ivec2> DragSmoother::consume_click()
{
    auto click = pending_click_;
    pending_click_.reset();
    return click;
}

std::optional<glm::ivec2> DragSmoother::consume_double_click()
{
    auto click = pending_double_click_;
    pending_double_click_.reset();
    return click;
}

} // namespace draxul::camera_input
