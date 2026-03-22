#pragma once

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
class HostManager;
class SdlWindow;
struct SplitLayout;
struct KeyEvent;
struct MouseButtonEvent;
struct MouseMoveEvent;
struct MouseWheelEvent;

// Wires SDL window input callbacks and routes events:
//   - Key events: checks GUI keybindings first (via GuiActionHandler), then forwards to
//     UiPanel/host
//   - Mouse events: forwards to UiPanel when inside the panel, otherwise to host
//   - Text/editing events: forwarded directly to UiPanel and host
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
        UiPanel* ui_panel = nullptr;
        IHost* host = nullptr;
        // Multi-pane: optional. If set, mouse events are hit-tested against split_layout.
        HostManager* host_manager = nullptr;
        SplitLayout* split_layout = nullptr;
        // Callback to notify App that focused pane changed (so App can update focused_slot).
        std::function<void(int)> on_pane_focus_changed;
        bool smooth_scroll = false;
        float scroll_speed = 1.0f;

        std::function<void()> request_frame;
        std::function<void(int, int)> on_resize;
        std::function<void(float)> on_display_scale_changed;
    };

    explicit InputDispatcher(Deps deps);

    // Installs this dispatcher's lambdas as the window's event callbacks.
    void connect(SdlWindow& window);

    // Updates the host pointer (used when focus changes between panes).
    void set_host(IHost* host)
    {
        deps_.host = host;
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
};

} // namespace draxul
