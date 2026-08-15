#include "megacity_camera_input.h"

#include <draxul/isometric_camera.h>
#include <glm/geometric.hpp>

namespace draxul
{
namespace
{
constexpr float kMovementSpeedFractionPerSecond = 0.35f;
constexpr float kOrbitSpeedRadiansPerSecond = 1.8f;
constexpr float kZoomSpeedPerSecond = 1.35f;
constexpr float kPitchSpeedRadiansPerSecond = 0.9f;
}

void MegacityCameraInput::reset_keys()
{
    input_.reset_keys();
}

bool MegacityCameraInput::on_key(const KeyEvent& event)
{
    return input_.on_key(event);
}

bool MegacityCameraInput::on_mouse_move(const MouseMoveEvent& event, IsometricCamera& camera)
{
    return input_.on_mouse_move(event, camera);
}

void MegacityCameraInput::on_mouse_button(const MouseButtonEvent& event)
{
    input_.on_mouse_button(event);
}

MegacityCameraInputFrame MegacityCameraInput::update(float dt, float world_span, IsometricCamera& camera)
{
    MegacityCameraInputFrame result;
    if (input_.movement_active())
    {
        const CameraMovement movement = input_.movement();
        const float pan_distance = dt * kMovementSpeedFractionPerSecond * world_span;
        if (pan_distance > 0.0f && glm::dot(movement.pan_input, movement.pan_input) > 0.0f)
        {
            const glm::vec2 right = camera.planar_right_vector();
            const glm::vec2 up = camera.planar_up_vector();
            const glm::vec2 pan = glm::normalize(movement.pan_input.x * right + movement.pan_input.y * up);
            camera.translate_target(pan.x * pan_distance, pan.y * pan_distance);
            result.camera_changed = true;
        }
        if (movement.orbit != 0.0f && dt > 0.0f)
        {
            camera.orbit_target(movement.orbit * dt * kOrbitSpeedRadiansPerSecond);
            result.camera_changed = true;
        }
        if (movement.zoom != 0.0f && dt > 0.0f)
        {
            camera.zoom_by(movement.zoom * dt * kZoomSpeedPerSecond);
            result.camera_changed = true;
        }
        if (movement.pitch != 0.0f && dt > 0.0f)
        {
            camera.adjust_pitch(movement.pitch * dt * kPitchSpeedRadiansPerSecond);
            result.camera_changed = true;
        }
    }

    result.camera_changed |= input_.apply_drag_smoothing(dt, camera);
    result.double_click = input_.consume_double_click();
    if (!result.double_click)
        result.click = input_.consume_click();
    return result;
}

bool MegacityCameraInput::movement_active() const
{
    return input_.movement_active();
}

bool MegacityCameraInput::drag_smoothing_active() const
{
    return input_.drag_smoothing_active();
}

} // namespace draxul
