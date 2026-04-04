#pragma once

#include <draxul/pixel_scale.h>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace draxul
{

struct GuiKeybinding;
class GuiActionHandler;
class UiPanel;
class IHost;
class IWindow;
class HostManager;
class SdlWindow;
struct KeyEvent;
struct MouseButtonEvent;
struct MouseMoveEvent;
struct MouseWheelEvent;

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
    struct Deps
    {
        const std::vector<GuiKeybinding>* keybindings = nullptr;
        GuiActionHandler* gui_action_handler = nullptr;
        // Returns the active overlay host (e.g. command palette) when it should
        // intercept all input, or null when no overlay is active.
        std::function<IHost*()> overlay_host;
        UiPanel* ui_panel = nullptr;
        IHost* host = nullptr;
        // Multi-pane: if set, mouse events are hit-tested via HostManager's SplitTree.
        // Function-based so it always returns the active workspace's HostManager.
        std::function<HostManager*()> host_manager;
        bool smooth_scroll = false;
        float scroll_speed = 1.0f;
        // Ratio of physical pixels to logical pixels (1.0 on non-HiDPI, 2.0 on Retina).
        // Used to convert SDL logical mouse coordinates to physical pixels for hit-testing
        // pane descriptors (which are stored in physical pixels) and for forwarding to hosts.
        PixelScale pixel_scale;

        std::function<void()> request_frame;
        std::function<void(int, int)> on_resize;
        std::function<void(float)> on_display_scale_changed;
        // Tab bar click: returns 1-based tab index if (px, py) hits a tab, else 0.
        std::function<int(int, int)> hit_test_tab;
        // Activate tab by 1-based index.
        std::function<void(int)> activate_tab;
    };

    explicit InputDispatcher(Deps deps);

    // Installs this dispatcher's lambdas as the window's event callbacks.
    void connect(IWindow& window);

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

    // Exposed for testing — checks if the key event matches any GUI keybinding.
    std::optional<std::string_view> gui_action_for_key_event(const KeyEvent& event) const;

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
    void on_key_event(const KeyEvent& event);
    void on_mouse_button_event(const MouseButtonEvent& event);
    void on_mouse_move_event(const MouseMoveEvent& event);
    void on_mouse_wheel_event(const MouseWheelEvent& event);
    // Returns the host that should receive mouse events at (px, py).
    IHost* host_for_mouse_pos(int px, int py);

    Deps deps_;
    float pending_scroll_y_ = 0.0f;
    bool had_scroll_event_ = false;
    // Chord (tmux-style prefix) state: true when a prefix key has been consumed and
    // we are waiting for the second key of a chord binding.
    bool prefix_active_ = false;
    // Set when a chord action fires; causes the immediately following text-input event
    // (SDL_EVENT_TEXT_INPUT for the chord's second key) to be suppressed.
    bool suppress_next_text_input_ = false;
};

} // namespace draxul
