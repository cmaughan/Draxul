#pragma once

#include <draxul/codeviz_input_state.h>

namespace draxul
{

class IsometricCamera;

struct MegacityCameraInputFrame
{
    bool camera_changed = false;
    std::optional<glm::ivec2> click;
    std::optional<glm::ivec2> double_click;
};

// Owns the generic CodeViz input state and applies its per-frame movement to
// the Megacity camera. Host-specific selection shortcuts remain in the host.
class MegacityCameraInput
{
public:
    void reset_keys();
    bool on_key(const KeyEvent& event);
    bool on_mouse_move(const MouseMoveEvent& event, IsometricCamera& camera);
    void on_mouse_button(const MouseButtonEvent& event);

    MegacityCameraInputFrame update(float dt, float world_span, IsometricCamera& camera);

    bool movement_active() const;
    bool drag_smoothing_active() const;

private:
    CodeVizInputState input_;
};

} // namespace draxul
