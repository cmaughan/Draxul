#pragma once

#include <draxul/events.h>
#include <glm/vec2.hpp>
#include <optional>

namespace draxul
{

class IsometricCamera;

struct CameraMovement
{
    glm::vec2 pan_input{ 0.0f };
    float orbit = 0.0f;
    float zoom = 0.0f;
    float pitch = 0.0f;
};

// Self-contained input state machine for the megacity view.
// Translates SDL key/mouse events into camera movement intent.
class CityInputState
{
public:
    // Clear all pressed-key state (called when the host loses focus).
    void reset_keys();

    // Returns true if the key was consumed and state changed.
    bool on_key(const KeyEvent& event);
    void on_mouse_button(const MouseButtonEvent& event);

    // Accumulates drag pan/orbit deltas. Returns true if drag state changed.
    bool on_mouse_move(const MouseMoveEvent& event, IsometricCamera& camera);

    bool movement_active() const;
    bool drag_smoothing_active() const;
    CameraMovement movement() const;

    // Returns a click position if the last mouse-up was a click (not a drag).
    // Consumes the click — subsequent calls return nullopt until the next click.
    std::optional<glm::ivec2> consume_click();

    // Apply pending drag smoothing for this frame tick.
    // Returns true if the camera was modified.
    bool apply_drag_smoothing(float dt, IsometricCamera& camera);

private:
    bool move_left_ = false;
    bool move_right_ = false;
    bool move_up_ = false;
    bool move_down_ = false;
    bool orbit_left_ = false;
    bool orbit_right_ = false;
    bool zoom_in_ = false;
    bool zoom_out_ = false;
    bool pitch_up_ = false;
    bool pitch_down_ = false;
    bool dragging_scene_ = false;
    glm::vec2 pending_drag_pan_{ 0.0f };
    float pending_drag_orbit_ = 0.0f;
    glm::ivec2 last_drag_pos_{ 0 };
    glm::ivec2 press_pos_{ 0 };
    bool was_dragged_ = false;
    std::optional<glm::ivec2> pending_click_;
};

} // namespace draxul
