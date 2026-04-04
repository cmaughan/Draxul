#include "input_dispatcher.h"

#include "gui_action_handler.h"
#include "host_manager.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <draxul/app_config.h>
#include <draxul/events.h>
#include <draxul/host.h>
#include <draxul/perf_timing.h>
#include <draxul/ui_panel.h>
#include <draxul/window.h>

namespace draxul
{

InputDispatcher::InputDispatcher(Deps deps)
    : deps_(std::move(deps))
{
}

// Returns the host that should receive a mouse event at (px, py).
// px, py are SDL logical coordinates. PaneDescriptor boundaries are stored in
// physical pixels, so we scale before hit-testing.
IHost* InputDispatcher::host_for_mouse_pos(int px, int py)
{
    PERF_MEASURE();
    HostManager* hm = deps_.host_manager ? deps_.host_manager() : nullptr;
    if (hm)
    {
        const int phys_x = deps_.pixel_scale.to_physical(px);
        const int phys_y = deps_.pixel_scale.to_physical(py);
        IHost* target = hm->host_at_point(phys_x, phys_y);
        if (target)
        {
            deps_.host = target;
            return target;
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

template <typename Deps>
void request_imgui_frame_if_needed(const Deps& deps)
{
    if (deps.ui_panel->wants_keyboard() && deps.request_frame)
        deps.request_frame();
}

void InputDispatcher::on_key_event(const KeyEvent& event)
{
    PERF_MEASURE();

    // Overlay host (e.g. command palette) intercepts all input when active.
    if (deps_.overlay_host)
    {
        if (IHost* overlay = deps_.overlay_host())
        {
            overlay->on_key(event);
            deps_.ui_panel->on_key(event);
            request_imgui_frame_if_needed(deps_);
            return;
        }
    }

    if (event.pressed)
    {
        if (prefix_active_)
        {
            // Modifier-only keys (Shift, Ctrl, Alt, Super) don't cancel prefix mode —
            // the chord's second key may itself require a modifier (e.g. Shift+\ for |).
            if (is_modifier_only_key(event))
            {
                deps_.ui_panel->on_key(event);
                request_imgui_frame_if_needed(deps_);
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
    request_imgui_frame_if_needed(deps_);
    if (!deps_.ui_panel->wants_keyboard() && deps_.host)
        deps_.host->on_key(event);
}

void InputDispatcher::on_mouse_button_event(const MouseButtonEvent& event)
{
    PERF_MEASURE();

    // Tab bar click — check before anything else.
    if (event.pressed && deps_.hit_test_tab && deps_.activate_tab)
    {
        const int phys_x = deps_.pixel_scale.to_physical(event.pos.x);
        const int phys_y = deps_.pixel_scale.to_physical(event.pos.y);
        if (int tab_index = deps_.hit_test_tab(phys_x, phys_y); tab_index > 0)
        {
            deps_.activate_tab(tab_index);
            return;
        }
    }

    deps_.ui_panel->on_mouse_button(event);
    IHost* target = host_for_mouse_pos(event.pos.x, event.pos.y);
    if (deps_.ui_panel->wants_mouse() && deps_.request_frame)
        deps_.request_frame();
    if (deps_.ui_panel->wants_mouse())
        return;
    if (deps_.ui_panel->layout().contains_panel_point(deps_.pixel_scale.to_physical(event.pos.x), deps_.pixel_scale.to_physical(event.pos.y)))
    {
        return;
    }
    if (target)
    {
        // Hosts store viewports and cell sizes in physical pixels; translate
        // the SDL logical coordinates to physical before forwarding.
        MouseButtonEvent phys = event;
        phys.pos.x = deps_.pixel_scale.to_physical(event.pos.x);
        phys.pos.y = deps_.pixel_scale.to_physical(event.pos.y);
        target->on_mouse_button(phys);
    }
}

void InputDispatcher::on_mouse_move_event(const MouseMoveEvent& event)
{
    PERF_MEASURE();
    deps_.ui_panel->on_mouse_move(event);
    IHost* target = host_for_mouse_pos(event.pos.x, event.pos.y);
    if (deps_.ui_panel->wants_mouse() && deps_.request_frame)
        deps_.request_frame();
    if (deps_.ui_panel->wants_mouse())
        return;
    if (deps_.ui_panel->layout().contains_panel_point(deps_.pixel_scale.to_physical(event.pos.x), deps_.pixel_scale.to_physical(event.pos.y)))
    {
        return;
    }
    if (target)
    {
        MouseMoveEvent phys = event;
        phys.pos.x = deps_.pixel_scale.to_physical(event.pos.x);
        phys.pos.y = deps_.pixel_scale.to_physical(event.pos.y);
        phys.delta *= deps_.pixel_scale.value();
        target->on_mouse_move(phys);
    }
}

void InputDispatcher::on_mouse_wheel_event(const MouseWheelEvent& event)
{
    PERF_MEASURE();
    deps_.ui_panel->on_mouse_wheel(event);
    IHost* wheel_host = host_for_mouse_pos(event.pos.x, event.pos.y);
    if (deps_.ui_panel->wants_mouse() && deps_.request_frame)
        deps_.request_frame();
    if (deps_.ui_panel->wants_mouse())
        return;
    if (deps_.ui_panel->layout().contains_panel_point(deps_.pixel_scale.to_physical(event.pos.x), deps_.pixel_scale.to_physical(event.pos.y)))
        return;
    if (!wheel_host)
        return;

    // Build a physical-coordinate version of the event for forwarding to the host.
    MouseWheelEvent phys_event = event;
    phys_event.pos.x = deps_.pixel_scale.to_physical(event.pos.x);
    phys_event.pos.y = deps_.pixel_scale.to_physical(event.pos.y);

    if (deps_.smooth_scroll && event.delta.y != 0.0f)
    {
        had_scroll_event_ = true;
        pending_scroll_y_ += event.delta.y * deps_.scroll_speed;
        const float sign = pending_scroll_y_ > 0.0f ? 1.0f : -1.0f;
        const auto steps = static_cast<int>(std::abs(pending_scroll_y_));
        for (int i = 0; i < steps; ++i)
        {
            MouseWheelEvent step = phys_event;
            step.delta.y = sign;
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

void InputDispatcher::set_host(IHost* host)
{
    if (deps_.host && deps_.host != host)
        deps_.host->on_focus_lost();
    deps_.host = host;
}

void InputDispatcher::connect(IWindow& window)
{
    PERF_MEASURE();
    window.on_key = [this](const KeyEvent& e) { on_key_event(e); };

    window.on_text_input = [this](const TextInputEvent& event) {
        if (suppress_next_text_input_)
        {
            suppress_next_text_input_ = false;
            return;
        }
        // Overlay host consumes text input when active.
        if (deps_.overlay_host)
        {
            if (IHost* overlay = deps_.overlay_host())
            {
                overlay->on_text_input(event);
                request_imgui_frame_if_needed(deps_);
                return;
            }
        }
        deps_.ui_panel->on_text_input(event);
        request_imgui_frame_if_needed(deps_);
        if (!deps_.ui_panel->wants_keyboard() && deps_.host)
            deps_.host->on_text_input(event);
    };

    window.on_text_editing = [this](const TextEditingEvent& event) {
        request_imgui_frame_if_needed(deps_);
        if (deps_.host)
            deps_.host->on_text_editing(event);
    };

    window.on_mouse_button = [this](const MouseButtonEvent& e) { on_mouse_button_event(e); };
    window.on_mouse_move = [this](const MouseMoveEvent& e) { on_mouse_move_event(e); };
    window.on_mouse_wheel = [this](const MouseWheelEvent& e) { on_mouse_wheel_event(e); };

    window.on_resize = [this](const WindowResizeEvent& event) {
        if (deps_.on_resize)
            deps_.on_resize(event.size.x, event.size.y);
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
    PERF_MEASURE();
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
