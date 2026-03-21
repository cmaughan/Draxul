#include "input_dispatcher.h"

#include "gui_action_handler.h"
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

void InputDispatcher::on_key_event(const KeyEvent& event)
{
    if (event.pressed)
    {
        if (auto action = gui_action_for_key_event(event);
            action && deps_.gui_action_handler && deps_.gui_action_handler->execute(*action))
            return;
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
    if (deps_.host)
        deps_.host->on_mouse_button(event);
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
    if (deps_.host)
        deps_.host->on_mouse_move(event);
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
    if (!deps_.host)
        return;

    if (deps_.smooth_scroll && event.dy != 0.0f)
    {
        pending_scroll_y_ += event.dy;
        const float sign = pending_scroll_y_ > 0.0f ? 1.0f : -1.0f;
        const auto steps = static_cast<int>(std::abs(pending_scroll_y_));
        for (int i = 0; i < steps; ++i)
        {
            MouseWheelEvent step = event;
            step.dy = sign;
            deps_.host->on_mouse_wheel(step);
        }
        pending_scroll_y_ = std::fmod(pending_scroll_y_, 1.0f);
        if (deps_.request_frame)
            deps_.request_frame();
    }
    else
    {
        pending_scroll_y_ = 0.0f;
        deps_.host->on_mouse_wheel(event);
    }
}

void InputDispatcher::connect(SdlWindow& window)
{
    window.on_key = [this](const KeyEvent& e) { on_key_event(e); };

    window.on_text_input = [this](const TextInputEvent& event) {
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
        if (gui_keybinding_matches(binding, event))
            return binding.action;
    }
    return std::nullopt;
}

} // namespace draxul
