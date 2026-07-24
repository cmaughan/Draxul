#pragma once

#include "split_tree.h"

#include <algorithm>
#include <chrono>
#include <draxul/pixel_scale.h>
#include <draxul/types.h>
#include <draxul/window.h>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

struct GuiKeybinding;
class GuiActionHandler;
class UiPanel;
class IHost;
class PaneManager;
class SdlWindow;
struct KeyEvent;
struct MouseButtonEvent;
struct MouseMoveEvent;
struct MouseWheelEvent;

class IInputRouter
{
public:
    virtual ~IInputRouter() = default;
    virtual IHost* overlay_host() = 0;
    virtual PaneManager* pane_manager() = 0;
    virtual int hit_test_space(int phys_x, int phys_y) = 0;
    virtual int hit_test_agent(int phys_x, int phys_y) = 0;
    virtual int hit_test_tab(int phys_x, int phys_y) = 0;
    virtual LeafId hit_test_pane_pill(int phys_x, int phys_y) = 0;
    virtual bool hit_test_app_chrome(int phys_x, int phys_y) = 0;
    virtual bool hit_test_shell_divider(int phys_x, int phys_y) = 0;
    virtual void resize_space_sidebar(int phys_x) = 0;
    virtual std::pair<int, int> cell_size_phys() = 0;
    virtual void activate_tab(int one_based_index) = 0;
    virtual void activate_space(int space_id) = 0;
    virtual void activate_agent(int one_based_index) = 0;
    virtual void activate_pane(int one_based_index) = 0;
    virtual void begin_tab_rename(int one_based_index) = 0;
    virtual void begin_pane_rename(LeafId leaf) = 0;
    virtual bool is_editing() = 0;
    virtual bool rename_text_input(const std::string& text) = 0;
    virtual bool rename_key(int keycode) = 0;
    virtual void commit_rename() = 0;
};

// Wires SDL window input callbacks and routes events:
//   - Key events: checks GUI keybindings first (via GuiActionHandler), then forwards to
//     UiPanel/host
//   - Mouse events: forwards to UiPanel when inside the panel, otherwise to host
//   - Text-editing events: forwarded directly to the host
//
// App creates one InputDispatcher after all subsystems are ready and calls connect() to
// install it as the window's event handlers.
class InputDispatcher
{
public:
    struct ChordIndicatorState
    {
        std::string text;
        float alpha = 0.0f;

        [[nodiscard]] bool visible() const
        {
            return !text.empty() && alpha > 0.0f;
        }
    };

    struct Deps
    {
        const std::vector<GuiKeybinding>* keybindings = nullptr;
        GuiActionHandler* gui_action_handler = nullptr;
        // Window used for cursor changes and pixel size queries during divider drag.
        IWindow* window = nullptr;
        // Typed routing interface for chrome, overlay, and split-tree hit tests.
        IInputRouter* router = nullptr;
        UiPanel* ui_panel = nullptr;
        IHost* host = nullptr;
        bool smooth_scroll = false;
        float scroll_speed = 1.0f;
        // Ratio of physical pixels to logical pixels (1.0 on non-HiDPI, 2.0 on Retina).
        // Used to convert SDL logical mouse coordinates to physical pixels for hit-testing
        // pane descriptors (which are stored in physical pixels) and for forwarding to hosts.
        PixelScale pixel_scale;

        std::function<void()> request_frame;
        std::function<void()> on_layout_changed;
        std::function<void(int, int)> on_resize;
        std::function<void(float)> on_display_scale_changed;
    };

    explicit InputDispatcher(Deps deps);
    ~InputDispatcher() = default;
    InputDispatcher(const InputDispatcher&) = delete;
    InputDispatcher& operator=(const InputDispatcher&) = delete;
    InputDispatcher(InputDispatcher&&) = delete;
    InputDispatcher& operator=(InputDispatcher&&) = delete;

    // Installs this dispatcher's lambdas as the window's event callbacks.
    void connect(IWindow& window);
    // Releases the installed callback token. Safe to call repeatedly and
    // required before replacing dependencies or beginning owner teardown.
    void disconnect();
    void reconfigure(Deps deps);

    // Updates the host pointer (used when focus changes between panes).
    void set_host(IHost* host);

    // Updates the pixel scale (called when the display DPI changes).
    void set_pixel_scale(PixelScale scale)
    {
        deps_.pixel_scale = scale;
    }

    void set_scroll_config(bool smooth_scroll, float scroll_speed)
    {
        deps_.smooth_scroll = smooth_scroll;
        deps_.scroll_speed = scroll_speed;
    }

    void set_chord_indicator_fade_ms(int fade_ms);

    // Exposed for testing — checks if the key event matches any GUI keybinding.
    std::optional<std::string_view> gui_action_for_key_event(const KeyEvent& event) const;
    [[nodiscard]] ChordIndicatorState chord_indicator_state(std::chrono::steady_clock::time_point now) const;
    bool update(std::chrono::steady_clock::time_point now, int chord_timeout_ms);

    // The fractional portion of accumulated scroll (in cells) not yet committed to the host.
    // Only valid if had_scroll_event() is true; returns 0 when no wheel event arrived this frame.
    // Positive = pending upward scroll; negative = pending downward scroll.
    float scroll_fraction() const
    {
        return had_scroll_event_ ? pending_scroll_y_ : 0.0f;
    }

    // True if at least one mouse-wheel event was processed since the last clear_scroll_event().
    bool had_scroll_event() const
    {
        return had_scroll_event_;
    }

    // Called once per frame (after applying the scroll offset) to clear the per-frame flag.
    // The fractional accumulator (pending_scroll_y_) is intentionally preserved so that
    // slow trackpad gestures can accumulate across events; only the visual-offset flag is reset.
    void clear_scroll_event()
    {
        had_scroll_event_ = false;
    }

private:
    void start_indicator_fade(std::chrono::steady_clock::time_point now);
    void on_key_event(const KeyEvent& event);
    void on_mouse_button_event(const MouseButtonEvent& event);
    void on_mouse_move_event(const MouseMoveEvent& event);
    void on_mouse_wheel_event(const MouseWheelEvent& event);
    // Returns the host that should receive mouse events at (px, py).
    IHost* host_for_mouse_pos(int px, int py);
    // Update mouse cursor based on whether (phys_x, phys_y) is over a divider.
    // Returns true if a divider is under the point. Skipped while dragging.
    bool update_cursor_for_divider(int phys_x, int phys_y);
    void set_active_mouse_cursor(MouseCursor cursor);
    // Shared mouse-event tail used by all three on_mouse_*_event handlers.
    // Resolves target via host_for_mouse_pos, runs the UiPanel wants_mouse
    // short-circuit (with request_frame side effect), the contains_panel_point
    // suppression, and finally calls `forward(*target, phys_x, phys_y)` only
    // when an event should reach a host. Each handler is responsible for its
    // own pre-checks (overlay, chrome strip, divider drag, panel-forward) and
    // the per-event-type physical-event construction inside `forward`.
    void dispatch_mouse_to_host(
        int logical_x,
        int logical_y,
        const std::function<void(IHost&, int phys_x, int phys_y)>& forward);

    Deps deps_;
    // Divider drag state.
    bool dragging_shell_divider_ = false;
    int drag_divider_id_ = -1; // kInvalidDivider
    MouseCursor active_mouse_cursor_ = MouseCursor::Default;
    float pending_scroll_y_ = 0.0f;
    bool had_scroll_event_ = false;
    // Chord (tmux-style prefix) state: true when a prefix key has been consumed and
    // we are waiting for the second key of a chord binding.
    bool prefix_active_ = false;
    // Set when a chord action fires; causes the immediately following text-input event
    // (SDL_EVENT_TEXT_INPUT for the chord's second key) to be suppressed.
    bool suppress_next_text_input_ = false;
    bool pane_select_active_ = false;
    std::string indicator_text_;
    std::optional<std::chrono::steady_clock::time_point> prefix_started_at_;
    std::optional<std::chrono::steady_clock::time_point> fade_started_at_;
    std::optional<std::chrono::steady_clock::time_point> fade_ends_at_;
    int chord_indicator_fade_ms_ = 2500;
    IWindow::CallbackConnection window_connection_;
};

} // namespace draxul
