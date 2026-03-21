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
class SdlWindow;
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
        bool smooth_scroll = false;

        std::function<void()> request_frame;
        std::function<void(int, int)> on_resize;
        std::function<void(float)> on_display_scale_changed;
    };

    explicit InputDispatcher(Deps deps);

    // Installs this dispatcher's lambdas as the window's event callbacks.
    void connect(SdlWindow& window);

    // Exposed for testing — checks if the key event matches any GUI keybinding.
    std::optional<std::string_view> gui_action_for_key_event(const KeyEvent& event) const;

    // The fractional portion of accumulated scroll (in cells) not yet committed to the host.
    // Positive = pending upward scroll; negative = pending downward scroll.
    float scroll_fraction() const
    {
        return pending_scroll_y_;
    }

private:
    void on_key_event(const KeyEvent& event);
    void on_mouse_button_event(const MouseButtonEvent& event);
    void on_mouse_move_event(const MouseMoveEvent& event);
    void on_mouse_wheel_event(const MouseWheelEvent& event);

    Deps deps_;
    float pending_scroll_y_ = 0.0f;
};

} // namespace draxul
