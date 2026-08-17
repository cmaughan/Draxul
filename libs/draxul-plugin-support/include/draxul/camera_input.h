#pragma once

// Shared camera *input* layer for 3D product plugins: the key->latch->movement
// table and the drag-inertia/click state machine. MegaCity and SatView each had
// their own copy of the same binding table with drifted semantics (audit cluster
// H). The camera MATH is genuinely different per product and stays there.
//
// Depends only on draxul/events.h (draxul-types) and SDL3 headers so it can be
// an allowlisted Draxul::PluginSupport::CameraInput leaf.

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

#include <chrono>
#include <cstdint>
#include <draxul/events.h>
#include <glm/vec2.hpp>
#include <optional>

namespace draxul::camera_input
{

// ---- Axis semantics ---------------------------------------------------------
//
// THE W/S DRIFT, RESOLVED: MegaCity bound W/S (and Up/Down) to PAN; SatView
// bound them to vertical ORBIT, and additionally folded A/D into horizontal
// orbit rather than pan. Neither mapping is documented as the intended one and
// each is right for its camera (MegaCity's is a pannable isometric city view;
// SatView's is a globe you can only orbit).
//
// So the shared table does NOT pick a winner: it latches the four key GROUPS
// separately and reports them as distinct axes, and each product binds the
// groups to the axes its camera understands via OrbitKeyBindings. Both products
// keep exactly the behaviour they had.
struct OrbitMovement
{
    // Arrow/WASD group. x: left(-1)/right(+1), y: down(-1)/up(+1).
    glm::vec2 pan{ 0.0f };
    // Q/E (x) and T/G (y) group, plus whatever the bindings fold in.
    glm::vec2 orbit{ 0.0f };
    // R/F.
    float zoom = 0.0f;
    // T/G reported separately for products with a dedicated pitch axis.
    float pitch = 0.0f;
};

struct OrbitKeyBindings
{
    // Fold the horizontal arrow/A/D group into `orbit.x` instead of `pan.x`
    // (SatView's globe has no horizontal pan).
    bool horizontal_arrows_orbit = false;
    // Fold the vertical arrow/W/S group into `orbit.y` instead of `pan.y`.
    bool vertical_arrows_orbit = false;
    // Report T/G on `orbit.y` as well as `pitch` (SatView treats pitch as
    // vertical orbit; MegaCity keeps them separate).
    bool pitch_folds_into_orbit = false;
    // Modifier mask that suppresses the zoom-in key. SatView guards Ctrl+R so
    // the host's reload accelerator is not swallowed by the camera; MegaCity
    // had no guard at all. Guarding is the canonical behaviour now — a product
    // that wants none sets this to 0.
    uint32_t zoom_in_guard_modifiers = kModCtrl;
};

// Latches the shared camera key table: arrows + WASD, Q/E orbit, R/F zoom,
// T/G pitch.
class OrbitKeyState
{
public:
    OrbitKeyState() = default;
    explicit OrbitKeyState(const OrbitKeyBindings& bindings)
        : bindings_(bindings)
    {
    }

    void set_bindings(const OrbitKeyBindings& bindings)
    {
        bindings_ = bindings;
    }
    [[nodiscard]] const OrbitKeyBindings& bindings() const
    {
        return bindings_;
    }

    // Clears every latch (call when the host loses focus).
    void reset();

    // Returns true when the key belonged to the table AND changed the latch.
    bool on_key(const KeyEvent& event);

    [[nodiscard]] bool movement_active() const;
    [[nodiscard]] OrbitMovement movement() const;

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

    OrbitKeyBindings bindings_{};
    bool left_ = false;
    bool right_ = false;
    bool up_ = false;
    bool down_ = false;
    bool orbit_left_ = false;
    bool orbit_right_ = false;
    bool zoom_in_ = false;
    bool zoom_out_ = false;
    bool pitch_up_ = false;
    bool pitch_down_ = false;
};

// ---- Drag smoothing + click detection ---------------------------------------
// Promoted from MegaCity's CodeVizInputState: accumulates drag deltas and bleeds
// them out with an exponential catch-up so dragging feels weighted, and
// distinguishes click / double-click from drag. The caller applies the consumed
// deltas to its own camera, so no camera type is referenced here.
struct DragSmootherConfig
{
    float catch_up_rate_per_second = 30.0f;
    float pan_settle_epsilon = 1e-4f;
    float orbit_settle_epsilon = 1e-4f;
    // Pixels of travel before a press stops counting as a click.
    int drag_threshold_px = 4;
    float double_click_max_distance_px = 4.0f;
    float double_click_max_time_ms = 400.0f;
};

// The portion of the accumulated drag to apply this tick.
struct DragStep
{
    glm::vec2 pan{ 0.0f };
    float orbit = 0.0f;
    [[nodiscard]] bool empty() const
    {
        return pan.x == 0.0f && pan.y == 0.0f && orbit == 0.0f;
    }
};

class DragSmoother
{
public:
    DragSmoother() = default;
    explicit DragSmoother(const DragSmootherConfig& config)
        : config_(config)
    {
    }

    // Left-button press/release; drives click and double-click detection.
    // `pressed_button` and `left_button` use the caller's button numbering
    // (SDL_BUTTON_LEFT for both products).
    void on_mouse_button(const MouseButtonEvent& event, int left_button);

    [[nodiscard]] bool dragging() const
    {
        return dragging_;
    }
    // Drops the drag without producing a click (e.g. the button was released
    // outside the pane and the platform never delivered the release).
    void cancel_drag()
    {
        dragging_ = false;
    }

    // Feeds a move while dragging. `pixel_delta` falls back to the difference
    // from the last position when the platform reports a zero delta. Returns
    // the pixel delta to convert into camera space, or nullopt when the move is
    // not part of a drag / has no travel.
    std::optional<glm::vec2> on_mouse_move(const MouseMoveEvent& event);

    // Accumulates already-converted deltas for later smoothing.
    void add_pan(const glm::vec2& pan)
    {
        pending_pan_ += pan;
    }
    void add_orbit(float orbit)
    {
        pending_orbit_ += orbit;
    }

    [[nodiscard]] bool smoothing_active() const;
    // Consumes this tick's share of the pending drag. Returns an empty step when
    // nothing is pending or dt is non-positive.
    DragStep consume_step(float dt);

    std::optional<glm::ivec2> consume_click();
    std::optional<glm::ivec2> consume_double_click();

private:
    DragSmootherConfig config_{};
    bool dragging_ = false;
    bool was_dragged_ = false;
    glm::ivec2 last_drag_pos_{ 0 };
    glm::ivec2 press_pos_{ 0 };
    glm::vec2 pending_pan_{ 0.0f };
    float pending_orbit_ = 0.0f;
    std::optional<glm::ivec2> pending_click_;
    std::optional<glm::ivec2> pending_double_click_;
    std::chrono::steady_clock::time_point last_click_time_{};
    glm::ivec2 last_click_pos_{ 0 };
    bool has_last_click_ = false;
};

} // namespace draxul::camera_input
