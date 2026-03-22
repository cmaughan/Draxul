#include "input_dispatcher.h"

#include "gui_action_handler.h"
#include "host_manager.h"
#include "split_layout.h"
#include <cmath>
#include <draxul/app_config.h>
#include <draxul/events.h>
#include <draxul/host.h>
#include <draxul/sdl_window.h>
#include <draxul/ui_panel.h>

namespace draxul
{

InputDispatcher::InputDispatcher(Deps deps)
    : deps_(std::move(deps))
{
}

// Convert a logical pixel coordinate to a physical pixel coordinate.
// SDL3 mouse events use logical (point) coordinates; pane descriptors and host
// viewports store physical (device) pixel coordinates. On Retina displays the
// two differ by the pixel_scale factor.
int InputDispatcher::to_physical(int logical) const
{
    return static_cast<int>(std::round(static_cast<float>(logical) * deps_.pixel_scale));
}

// Returns the host that should receive a mouse event at (px, py).
// px, py are SDL logical coordinates. PaneDescriptor boundaries are stored in
// physical pixels, so we scale before hit-testing.
IHost* InputDispatcher::host_for_mouse_pos(int px, int py)
{
    if (deps_.split_layout && deps_.host_manager)
    {
        const int phys_x = to_physical(px);
        const int phys_y = to_physical(py);
        const int pane_index = deps_.split_layout->hit_test(phys_x, phys_y);
        if (pane_index >= 0)
        {
            // Update focus if needed
            if (pane_index != deps_.split_layout->focused_pane_index)
            {
                deps_.split_layout->focused_pane_index = pane_index;
                if (deps_.on_pane_focus_changed)
                    deps_.on_pane_focus_changed(pane_index);
            }
            return deps_.host_manager->host_at(pane_index);
        }
    }
    return deps_.host;
}

static bool is_modifier_only_key(const KeyEvent& event)
{
    switch (event.keycode)
    {
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
    case SDLK_LCTRL:
    case SDLK_RCTRL:
    case SDLK_LALT:
    case SDLK_RALT:
    case SDLK_LGUI:
    case SDLK_RGUI:
        return true;
    default:
        return false;
    }
}

void InputDispatcher::on_key_event(const KeyEvent& event)
{
    if (event.pressed)
    {
        if (prefix_active_)
        {
            // Modifier-only keys (Shift, Ctrl, Alt, Super) don't cancel prefix mode —
            // the chord's second key may itself require a modifier (e.g. Shift+\ for |).
            if (is_modifier_only_key(event))
            {
                deps_.ui_panel->on_key(event);
                return;
            }
            // We consumed the prefix key; now look for a chord match.
            prefix_active_ = false;
            if (deps_.keybindings && deps_.gui_action_handler)
            {
                for (const auto& binding : *deps_.keybindings)
                {
                    if (binding.prefix_key != 0 && gui_keybinding_matches(binding, event))
                    {
                        suppress_next_text_input_ = true;
                        deps_.gui_action_handler->execute(binding.action);
                        return; // chord consumed — do not forward to host
                    }
                }
            }
            // No chord matched — fall through and forward to host normally.
        }
        else
        {
            // Check if this key activates a chord prefix.
            if (deps_.keybindings)
            {
                for (const auto& binding : *deps_.keybindings)
                {
                    if (gui_prefix_matches(binding, event))
                    {
                        prefix_active_ = true;
                        return; // swallow the prefix key
                    }
                }
            }
            // Check single-key (non-chord) GUI bindings.
            if (auto action = gui_action_for_key_event(event);
                action && deps_.gui_action_handler && deps_.gui_action_handler->execute(*action))
                return;
        }
    }
    else
    {
        // Key release — cancel prefix mode if the prefix key itself was released
        // (handles the case where the user holds the prefix key without pressing a chord key).
        // We don't cancel on every key-up, only on explicit Escape-like cancellation via
        // a future policy. For now just keep prefix_active_ until the next key-down.
    }
    deps_.ui_panel->on_key(event);
    if (!deps_.ui_panel->wants_keyboard() && deps_.host)
        deps_.host->on_key(event);
}

void InputDispatcher::on_mouse_button_event(const MouseButtonEvent& event)
{
    deps_.ui_panel->on_mouse_button(event);
    if (deps_.ui_panel->layout().contains_panel_point(event.x, event.y))
    {
        if (deps_.request_frame)
            deps_.request_frame();
        return;
    }
    IHost* target = host_for_mouse_pos(event.x, event.y);
    if (target)
    {
        // Hosts store viewports and cell sizes in physical pixels; translate
        // the SDL logical coordinates to physical before forwarding.
        MouseButtonEvent phys = event;
        phys.x = to_physical(event.x);
        phys.y = to_physical(event.y);
        target->on_mouse_button(phys);
    }
}

void InputDispatcher::on_mouse_move_event(const MouseMoveEvent& event)
{
    deps_.ui_panel->on_mouse_move(event);
    if (deps_.ui_panel->layout().contains_panel_point(event.x, event.y))
    {
        if (deps_.request_frame)
            deps_.request_frame();
        return;
    }
    IHost* target = host_for_mouse_pos(event.x, event.y);
    if (target)
    {
        MouseMoveEvent phys = event;
        phys.x = to_physical(event.x);
        phys.y = to_physical(event.y);
        target->on_mouse_move(phys);
    }
}

void InputDispatcher::on_mouse_wheel_event(const MouseWheelEvent& event)
{
    deps_.ui_panel->on_mouse_wheel(event);
    if (deps_.ui_panel->layout().contains_panel_point(event.x, event.y))
    {
        if (deps_.request_frame)
            deps_.request_frame();
        return;
    }
    IHost* wheel_host = host_for_mouse_pos(event.x, event.y);
    if (!wheel_host)
        return;

    // Build a physical-coordinate version of the event for forwarding to the host.
    MouseWheelEvent phys_event = event;
    phys_event.x = to_physical(event.x);
    phys_event.y = to_physical(event.y);

    if (deps_.smooth_scroll && event.dy != 0.0f)
    {
        had_scroll_event_ = true;
        pending_scroll_y_ += event.dy * deps_.scroll_speed;
        const float sign = pending_scroll_y_ > 0.0f ? 1.0f : -1.0f;
        const auto steps = static_cast<int>(std::abs(pending_scroll_y_));
        for (int i = 0; i < steps; ++i)
        {
            MouseWheelEvent step = phys_event;
            step.dy = sign;
            wheel_host->on_mouse_wheel(step);
        }
        pending_scroll_y_ = std::fmod(pending_scroll_y_, 1.0f);
        if (deps_.request_frame)
            deps_.request_frame();
    }
    else
    {
        pending_scroll_y_ = 0.0f;
        wheel_host->on_mouse_wheel(phys_event);
    }
}

void InputDispatcher::connect(SdlWindow& window)
{
    window.on_key = [this](const KeyEvent& e) { on_key_event(e); };

    window.on_text_input = [this](const TextInputEvent& event) {
        if (suppress_next_text_input_)
        {
            suppress_next_text_input_ = false;
            return;
        }
        deps_.ui_panel->on_text_input(event);
        if (!deps_.ui_panel->wants_keyboard() && deps_.host)
            deps_.host->on_text_input(event);
    };

    window.on_text_editing = [this](const TextEditingEvent& event) {
        if (deps_.host)
            deps_.host->on_text_editing(event);
    };

    window.on_mouse_button = [this](const MouseButtonEvent& e) { on_mouse_button_event(e); };
    window.on_mouse_move = [this](const MouseMoveEvent& e) { on_mouse_move_event(e); };
    window.on_mouse_wheel = [this](const MouseWheelEvent& e) { on_mouse_wheel_event(e); };

    window.on_resize = [this](const WindowResizeEvent& event) {
        if (deps_.on_resize)
            deps_.on_resize(event.width, event.height);
    };

    window.on_display_scale_changed = [this](const DisplayScaleEvent& event) {
        if (deps_.on_display_scale_changed)
            deps_.on_display_scale_changed(event.display_ppi);
    };

    window.on_drop_file = [this](std::string_view path) {
        if (deps_.host)
            deps_.host->dispatch_action(std::string("open_file:") + std::string(path));
    };
}

std::optional<std::string_view> InputDispatcher::gui_action_for_key_event(const KeyEvent& event) const
{
    if (!deps_.keybindings)
        return std::nullopt;
    for (const auto& binding : *deps_.keybindings)
    {
        // Skip chord bindings — those are only dispatched from the prefix state machine above.
        if (binding.prefix_key != 0)
            continue;
        if (gui_keybinding_matches(binding, event))
            return binding.action;
    }
    return std::nullopt;
}

} // namespace draxul
