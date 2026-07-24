#include "input_dispatcher.h"

#include "gui_action_handler.h"
#include "pane_manager.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <draxul/app_config.h>
#include <draxul/events.h>
#include <draxul/host.h>
#include <draxul/keybinding_parser.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/ui_panel.h>
#include <draxul/window.h>

namespace draxul
{

namespace
{

std::string describe_text_for_log(std::string_view text)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 4 + 2);
    out.push_back('"');
    for (unsigned char ch : text)
    {
        switch (ch)
        {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (ch >= 0x20 && ch <= 0x7E)
            {
                out.push_back(static_cast<char>(ch));
            }
            else
            {
                out += "\\x";
                out.push_back(kHex[(ch >> 4) & 0xF]);
                out.push_back(kHex[ch & 0xF]);
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

std::string host_trace_name(IHost* host)
{
    if (!host)
        return "<none>";
    const auto dbg = host->debug_state();
    return dbg.name.empty() ? "<unnamed>" : dbg.name;
}

} // namespace

InputDispatcher::InputDispatcher(Deps deps)
    : deps_(std::move(deps))
{
}

void InputDispatcher::disconnect()
{
    window_connection_.reset();
}

void InputDispatcher::reconfigure(Deps deps)
{
    disconnect();
    deps_ = std::move(deps);
    dragging_shell_divider_ = false;
    drag_divider_id_ = -1;
    active_mouse_cursor_ = MouseCursor::Default;
    pending_scroll_y_ = 0.0f;
    had_scroll_event_ = false;
    prefix_active_ = false;
    suppress_next_text_input_ = false;
    pane_select_active_ = false;
    indicator_text_.clear();
    prefix_started_at_.reset();
    fade_started_at_.reset();
    fade_ends_at_.reset();
}

void InputDispatcher::set_chord_indicator_fade_ms(int fade_ms)
{
    chord_indicator_fade_ms_ = std::max(100, fade_ms);
}

void InputDispatcher::start_indicator_fade(std::chrono::steady_clock::time_point now)
{
    const auto fade_duration = std::chrono::milliseconds(chord_indicator_fade_ms_);
    fade_started_at_ = now;
    fade_ends_at_ = now + fade_duration;
}

void InputDispatcher::set_active_mouse_cursor(MouseCursor cursor)
{
    if (!deps_.window || cursor == active_mouse_cursor_)
        return;
    deps_.window->set_mouse_cursor(cursor);
    active_mouse_cursor_ = cursor;
}

bool InputDispatcher::update_cursor_for_divider(int phys_x, int phys_y)
{
    PERF_MEASURE();
    if (!deps_.window)
        return false;
    if (deps_.router && deps_.router->hit_test_shell_divider(phys_x, phys_y))
    {
        set_active_mouse_cursor(MouseCursor::ResizeLeftRight);
        return true;
    }
    PaneManager* hm = deps_.router ? deps_.router->pane_manager() : nullptr;
    if (!hm)
        return false;
    auto hit = hm->divider_at_point(phys_x, phys_y);
    MouseCursor desired = MouseCursor::Default;
    if (hit)
    {
        if (hit->direction == SplitDirection::Vertical)
            desired = MouseCursor::ResizeLeftRight;
        else
            desired = MouseCursor::ResizeUpDown;
    }
    set_active_mouse_cursor(desired);
    return hit.has_value();
}

// Returns the host that should receive a mouse event at (px, py).
// px, py are SDL logical coordinates. PaneDescriptor boundaries are stored in
// physical pixels, so we scale before hit-testing.
IHost* InputDispatcher::host_for_mouse_pos(int px, int py)
{
    PERF_MEASURE();
    PaneManager* hm = deps_.router ? deps_.router->pane_manager() : nullptr;
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

static bool key_event_may_produce_text_input(const KeyEvent& event)
{
    if (!event.pressed)
        return false;
    return event.keycode == SDLK_SPACE
        || (event.keycode >= 0x20 && event.keycode <= 0x7E);
}

std::optional<int> digit_from_keycode(int32_t keycode)
{
    switch (keycode)
    {
    case SDLK_1:
        return 1;
    case SDLK_2:
        return 2;
    case SDLK_3:
        return 3;
    case SDLK_4:
        return 4;
    case SDLK_5:
        return 5;
    case SDLK_6:
        return 6;
    case SDLK_7:
        return 7;
    case SDLK_8:
        return 8;
    case SDLK_9:
        return 9;
    default:
        return std::nullopt;
    }
}

ModifierFlags normalize_modifiers(ModifierFlags mod)
{
    ModifierFlags result = kModNone;
    if (mod & kModShift)
        result |= kModShift;
    if (mod & kModCtrl)
        result |= kModCtrl;
    if (mod & kModAlt)
        result |= kModAlt;
    if (mod & kModSuper)
        result |= kModSuper;
    return result;
}

std::string chord_step_display(const KeyEvent& event)
{
    return format_gui_keybinding_combo(event.keycode, event.mod);
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
    const auto now = std::chrono::steady_clock::now();
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        const std::string combo = format_gui_keybinding_combo(event.keycode, event.mod);
        const std::string host_name = host_trace_name(deps_.host);
        log_printf(LogLevel::Trace, LogCategory::Input,
            "input trace: dispatcher key combo=%s key=%d mod=0x%X pressed=%d host=%p(%s) suppress_text=%d prefix=%d pane_select=%d",
            combo.c_str(),
            event.keycode,
            static_cast<unsigned int>(event.mod),
            event.pressed ? 1 : 0,
            static_cast<void*>(deps_.host),
            host_name.c_str(),
            suppress_next_text_input_ ? 1 : 0,
            prefix_active_ ? 1 : 0,
            pane_select_active_ ? 1 : 0);
    }

    // If a previously-consumed key never produced a text-input event, don't let
    // the stale suppression flag eat an unrelated later character.
    if (event.pressed && suppress_next_text_input_)
    {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
        {
            log_printf(LogLevel::Trace, LogCategory::Input,
                "input trace: dispatcher clearing stale suppress_next_text_input before handling new key");
        }
        suppress_next_text_input_ = false;
    }

    // Inline tab rename (WI 128) intercepts all key input when active —
    // typed characters arrive via on_text_input, but Enter/Escape/Backspace
    // and arrow keys must not leak to the host underneath.
    if (event.pressed && deps_.router && deps_.router->is_editing())
    {
        if (deps_.router->rename_key(event.keycode))
        {
            if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            {
                log_printf(LogLevel::Trace, LogCategory::Input,
                    "input trace: dispatcher key consumed by rename_key");
            }
            if (deps_.request_frame)
                deps_.request_frame();
            return;
        }
    }

    // Overlay host (e.g. command palette) intercepts all input when active.
    if (deps_.router)
    {
        if (IHost* overlay = deps_.router->overlay_host())
        {
            if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            {
                const std::string overlay_name = host_trace_name(overlay);
                log_printf(LogLevel::Trace, LogCategory::Input,
                    "input trace: dispatcher key routed to overlay host=%p(%s)",
                    static_cast<void*>(overlay),
                    overlay_name.c_str());
            }
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
                if (log_would_emit(LogLevel::Trace, LogCategory::Input))
                {
                    log_printf(LogLevel::Trace, LogCategory::Input,
                        "input trace: dispatcher key kept prefix mode alive with modifier-only key");
                }
                deps_.ui_panel->on_key(event);
                request_imgui_frame_if_needed(deps_);
                return;
            }
            // We consumed the prefix key; now look for a chord match.
            const std::string prefix_text = indicator_text_;
            prefix_active_ = false;
            prefix_started_at_.reset();
            if (pane_select_active_)
            {
                pane_select_active_ = false;
                const std::string final_text = prefix_text + " " + chord_step_display(event);
                if (auto digit = digit_from_keycode(event.keycode);
                    digit.has_value() && normalize_modifiers(event.mod) == kModNone && deps_.router)
                {
                    suppress_next_text_input_ = true;
                    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
                    {
                        log_printf(LogLevel::Trace, LogCategory::Input,
                            "input trace: dispatcher pane-select consumed key and enabled suppress_next_text_input");
                    }
                    indicator_text_ = final_text;
                    start_indicator_fade(now);
                    deps_.router->activate_pane(*digit);
                    if (deps_.request_frame)
                        deps_.request_frame();
                    return;
                }

                indicator_text_ = final_text;
                start_indicator_fade(now);
                if (deps_.request_frame)
                    deps_.request_frame();
                // No pane matched — fall through and forward to host normally.
            }
            else if (event.keycode == SDLK_0 && normalize_modifiers(event.mod) == kModNone && deps_.router)
            {
                pane_select_active_ = true;
                prefix_active_ = true;
                prefix_started_at_ = now;
                indicator_text_ = prefix_text + " 0";
                fade_started_at_.reset();
                fade_ends_at_.reset();
                suppress_next_text_input_ = true;
                if (deps_.request_frame)
                    deps_.request_frame();
                return;
            }
            if (deps_.keybindings && deps_.gui_action_handler)
            {
                for (const auto& binding : *deps_.keybindings)
                {
                    if (binding.prefix_key != 0 && gui_keybinding_matches(binding, event))
                    {
                        suppress_next_text_input_ = true;
                        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
                        {
                            log_printf(LogLevel::Trace, LogCategory::Input,
                                "input trace: dispatcher chord action=%.*s consumed key and enabled suppress_next_text_input",
                                static_cast<int>(binding.action.size()),
                                binding.action.data());
                        }
                        indicator_text_ = prefix_text + " " + chord_step_display(event);
                        start_indicator_fade(now);
                        deps_.gui_action_handler->execute(binding.action);
                        if (deps_.request_frame)
                            deps_.request_frame();
                        return; // chord consumed — do not forward to host
                    }
                }
            }
            indicator_text_ = prefix_text + " " + chord_step_display(event);
            start_indicator_fade(now);
            if (deps_.request_frame)
                deps_.request_frame();
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
                        pane_select_active_ = false;
                        prefix_started_at_ = now;
                        indicator_text_ = format_gui_keybinding_combo(binding.prefix_key, binding.prefix_modifiers);
                        fade_started_at_.reset();
                        fade_ends_at_.reset();
                        if (key_event_may_produce_text_input(event))
                            suppress_next_text_input_ = true;
                        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
                        {
                            log_printf(LogLevel::Trace, LogCategory::Input,
                                "input trace: dispatcher prefix action=%.*s consumed key prefix and suppress_next_text_input=%d",
                                static_cast<int>(binding.action.size()),
                                binding.action.data(),
                                suppress_next_text_input_ ? 1 : 0);
                        }
                        if (deps_.request_frame)
                            deps_.request_frame();
                        return; // swallow the prefix key
                    }
                }
            }
            // Check single-key (non-chord) GUI bindings.
            if (auto action = gui_action_for_key_event(event);
                action && deps_.gui_action_handler && deps_.gui_action_handler->execute(*action))
            {
                if (key_event_may_produce_text_input(event))
                    suppress_next_text_input_ = true;
                if (log_would_emit(LogLevel::Trace, LogCategory::Input))
                {
                    log_printf(LogLevel::Trace, LogCategory::Input,
                        "input trace: dispatcher gui action=%.*s consumed key and suppress_next_text_input=%d",
                        static_cast<int>(action->size()),
                        action->data(),
                        suppress_next_text_input_ ? 1 : 0);
                }
                return;
            }
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
    {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
        {
            const std::string host_name = host_trace_name(deps_.host);
            log_printf(LogLevel::Trace, LogCategory::Input,
                "input trace: dispatcher forwarding key to host=%p(%s)",
                static_cast<void*>(deps_.host),
                host_name.c_str());
        }
        deps_.host->on_key(event);
    }
}

void InputDispatcher::on_mouse_button_event(const MouseButtonEvent& event)
{
    PERF_MEASURE();
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace, LogCategory::Input,
            "input trace: dispatcher mouse_button button=%d pressed=%d clicks=%d mod=0x%X logical=(%d,%d)",
            event.button,
            event.pressed ? 1 : 0,
            event.clicks,
            static_cast<unsigned int>(event.mod),
            event.pos.x,
            event.pos.y);
    }

    // Overlay host consumes mouse button events — don't leak clicks to the
    // underlying host while e.g. the command palette is open.
    if (deps_.router && deps_.router->overlay_host())
    {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: dispatcher mouse_button swallowed by overlay");
        return;
    }

    const int phys_x = deps_.pixel_scale.to_physical(event.pos.x);
    const int phys_y = deps_.pixel_scale.to_physical(event.pos.y);
    const bool over_chrome = deps_.router
        && deps_.router->hit_test_app_chrome(phys_x, phys_y);

    if (event.button == SDL_BUTTON_LEFT && deps_.router)
    {
        if (!event.pressed && dragging_shell_divider_)
        {
            dragging_shell_divider_ = false;
            update_cursor_for_divider(phys_x, phys_y);
            return;
        }
        if (event.pressed && deps_.router->hit_test_shell_divider(phys_x, phys_y))
        {
            dragging_shell_divider_ = true;
            return;
        }
    }

    // Space sidebar click — activate the stable Space id represented by the row.
    if (event.pressed && deps_.router)
    {
        const int space_id = deps_.router->hit_test_space(phys_x, phys_y);
        if (space_id >= 0)
        {
            deps_.router->activate_space(space_id);
            return;
        }
    }

    // Agents sidebar click — resolve the derived row back to its pane owner.
    if (event.pressed && deps_.router)
    {
        const int agent_index = deps_.router->hit_test_agent(phys_x, phys_y);
        if (agent_index > 0)
        {
            deps_.router->activate_agent(agent_index);
            return;
        }
    }

    // Tab bar click — check before anything else.
    if (event.pressed && deps_.router)
    {
        if (int tab_index = deps_.router->hit_test_tab(phys_x, phys_y); tab_index > 0)
        {
            // Double-click on a tab → start inline rename. Single-click
            // activates as before.
            if (event.clicks >= 2)
                deps_.router->begin_tab_rename(tab_index);
            else
                deps_.router->activate_tab(tab_index);
            return;
        }
    }

    // Pane status pill click — same UX as tab clicks: double-click renames,
    // single-click is consumed (we don't want it to start drag selections in
    // the underlying terminal). The mouse move/release events for any drag
    // that started on a pill are also dropped via the over_pill check below.
    LeafId pane_pill_leaf = kInvalidLeaf;
    if (deps_.router)
        pane_pill_leaf = deps_.router->hit_test_pane_pill(phys_x, phys_y);
    if (event.pressed && pane_pill_leaf != kInvalidLeaf)
    {
        if (event.clicks >= 2 && deps_.router)
            deps_.router->begin_pane_rename(pane_pill_leaf);
        // Single click on the pill: just consume. Focus changes for that
        // pane already happen via the underlying pane click path; we don't
        // want a *pill* click to focus, since the pill sits inside the pane
        // it belongs to.
        return;
    }
    if (pane_pill_leaf != kInvalidLeaf)
    {
        // Mouse release on a pill: consume so drag-select doesn't start.
        return;
    }

    // Any click within the chrome strip is consumed — never forwarded to
    // the underlying host. Also commits an in-progress rename if the user
    // clicked on a chrome region that wasn't a tab.
    if (over_chrome)
    {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: dispatcher mouse_button swallowed by chrome");
        if (event.pressed && deps_.router && deps_.router->is_editing())
        {
            deps_.router->commit_rename();
        }
        return;
    }

    // Click outside the editing region (tab or pane) → commit the rename,
    // then continue dispatching the click so the user's intent (focus a
    // pane, etc.) still happens. is_editing_tab() reflects ANY active edit
    // session in the new unified state machine.
    if (event.pressed && deps_.router && deps_.router->is_editing())
    {
        deps_.router->commit_rename();
    }

    deps_.ui_panel->on_mouse_button(event);

    // Divider drag: capture on left-button press over a divider; release on
    // any button release. While dragging we suppress forwarding to hosts.
    if (event.button == SDL_BUTTON_LEFT)
    {
        if (event.pressed && drag_divider_id_ == kInvalidDivider)
        {
            PaneManager* hm = deps_.router ? deps_.router->pane_manager() : nullptr;
            if (hm)
            {
                if (auto hit = hm->divider_at_point(phys_x, phys_y))
                {
                    drag_divider_id_ = hit->id;
                    return;
                }
            }
        }
        else if (!event.pressed && drag_divider_id_ != kInvalidDivider)
        {
            drag_divider_id_ = kInvalidDivider;
            // Recompute cursor based on current position.
            update_cursor_for_divider(phys_x, phys_y);
            return;
        }
    }

    dispatch_mouse_to_host(event.pos.x, event.pos.y, [&event](IHost& target, int px, int py) {
        // Hosts store viewports and cell sizes in physical pixels; translate
        // the SDL logical coordinates to physical before forwarding.
        MouseButtonEvent phys = event;
        phys.pos.x = px;
        phys.pos.y = py;
        target.on_mouse_button(phys);
    });
}

void InputDispatcher::on_mouse_move_event(const MouseMoveEvent& event)
{
    PERF_MEASURE();

    // Overlay host consumes mouse move events — don't let hover/drag reach
    // the underlying host while an overlay (e.g. command palette) is active.
    if (deps_.router && deps_.router->overlay_host())
        return;

    const int phys_x_mv = deps_.pixel_scale.to_physical(event.pos.x);
    const int phys_y_mv = deps_.pixel_scale.to_physical(event.pos.y);

    if (dragging_shell_divider_)
    {
        if (deps_.router)
            deps_.router->resize_space_sidebar(phys_x_mv);
        if (deps_.request_frame)
            deps_.request_frame();
        return;
    }

    // Active divider drag: route directly to pane manager and skip the rest.
    // Handled before the chrome-strip suppression so a drag that strays into
    // the tab bar still updates the divider ratio.
    if (drag_divider_id_ != kInvalidDivider)
    {
        if (PaneManager* hm = deps_.router ? deps_.router->pane_manager() : nullptr)
        {
            int cw = 0, ch = 0;
            if (deps_.router)
            {
                const auto cs = deps_.router->cell_size_phys();
                cw = cs.first;
                ch = cs.second;
            }
            hm->update_divider_from_pixel(drag_divider_id_, phys_x_mv, phys_y_mv, cw, ch);
            if (deps_.on_layout_changed)
                deps_.on_layout_changed();
            if (deps_.request_frame)
                deps_.request_frame();
        }
        return;
    }

    // Hover cursor feedback: change to resize cursor when over a divider.
    // Skip while the panel wants the mouse.
    const bool over_divider = update_cursor_for_divider(phys_x_mv, phys_y_mv);
    if (over_divider)
        return;

    // Suppress moves over app chrome so a click-drag that started on a
    // navigation row or tab cannot become a selection in a pane.
    if (deps_.router && deps_.router->hit_test_app_chrome(phys_x_mv, phys_y_mv))
        return;

    MouseCursor desired = MouseCursor::Default;
    if (IHost* target = host_for_mouse_pos(event.pos.x, event.pos.y))
    {
        if (auto cursor = target->mouse_cursor_at(phys_x_mv, phys_y_mv))
            desired = *cursor;
    }
    set_active_mouse_cursor(desired);

    deps_.ui_panel->on_mouse_move(event);
    const float scale = deps_.pixel_scale.value();
    dispatch_mouse_to_host(event.pos.x, event.pos.y, [&event, scale](IHost& target, int px, int py) {
        MouseMoveEvent phys = event;
        phys.pos.x = px;
        phys.pos.y = py;
        phys.delta *= scale;
        target.on_mouse_move(phys);
    });
}

void InputDispatcher::on_mouse_wheel_event(const MouseWheelEvent& event)
{
    PERF_MEASURE();

    // Overlay host consumes scroll events — don't scroll the underlying
    // terminal while e.g. the command palette is open.
    if (deps_.router && deps_.router->overlay_host())
        return;

    // Suppress wheel events over the chrome strip — scrolling on the tab
    // bar should not scroll the terminal beneath.
    {
        const int phys_x = deps_.pixel_scale.to_physical(event.pos.x);
        const int phys_y = deps_.pixel_scale.to_physical(event.pos.y);
        if (deps_.router && deps_.router->hit_test_app_chrome(phys_x, phys_y))
            return;
    }

    deps_.ui_panel->on_mouse_wheel(event);
    dispatch_mouse_to_host(event.pos.x, event.pos.y, [this, &event](IHost& wheel_host, int px, int py) {
        // Build a physical-coordinate version of the event for forwarding to the host.
        MouseWheelEvent phys_event = event;
        phys_event.pos.x = px;
        phys_event.pos.y = py;

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
                wheel_host.on_mouse_wheel(step);
            }
            pending_scroll_y_ = std::fmod(pending_scroll_y_, 1.0f);
            if (deps_.request_frame)
                deps_.request_frame();
        }
        else
        {
            pending_scroll_y_ = 0.0f;
            wheel_host.on_mouse_wheel(phys_event);
        }
    });
}

void InputDispatcher::dispatch_mouse_to_host(
    int logical_x,
    int logical_y,
    const std::function<void(IHost&, int phys_x, int phys_y)>& forward)
{
    const int phys_x = deps_.pixel_scale.to_physical(logical_x);
    const int phys_y = deps_.pixel_scale.to_physical(logical_y);
    IHost* target = host_for_mouse_pos(logical_x, logical_y);
    if (deps_.ui_panel->wants_mouse() && deps_.request_frame)
        deps_.request_frame();
    if (deps_.ui_panel->wants_mouse())
    {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: dispatcher mouse event swallowed because ui_panel wants mouse");
        return;
    }
    if (deps_.ui_panel->layout().contains_panel_point(phys_x, phys_y))
    {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: dispatcher mouse event swallowed because pointer is over ui panel");
        return;
    }
    if (target)
    {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
        {
            const std::string host_name = host_trace_name(target);
            log_printf(LogLevel::Trace, LogCategory::Input,
                "input trace: dispatcher forwarding mouse event logical=(%d,%d) phys=(%d,%d) to host=%p(%s)",
                logical_x,
                logical_y,
                phys_x,
                phys_y,
                static_cast<void*>(target),
                host_name.c_str());
        }
        forward(*target, phys_x, phys_y);
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
    assert(deps_.ui_panel != nullptr && "InputDispatcher::Deps::ui_panel is required before connect()");
    disconnect();
    IWindow::InputCallbacks callbacks;
    callbacks.on_key = [this](const KeyEvent& e) { on_key_event(e); };

    callbacks.on_text_input = [this](const TextInputEvent& event) {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
        {
            const std::string described = describe_text_for_log(event.text);
            const std::string host_name = host_trace_name(deps_.host);
            log_printf(LogLevel::Trace, LogCategory::Input,
                "input trace: dispatcher text_input text=%s len=%zu host=%p(%s) suppress_text=%d",
                described.c_str(),
                event.text.size(),
                static_cast<void*>(deps_.host),
                host_name.c_str(),
                suppress_next_text_input_ ? 1 : 0);
        }
        if (suppress_next_text_input_)
        {
            suppress_next_text_input_ = false;
            if (log_would_emit(LogLevel::Trace, LogCategory::Input))
                log_printf(LogLevel::Trace, LogCategory::Input, "input trace: dispatcher swallowed text_input because suppress_next_text_input was set");
            return;
        }
        // Inline tab rename consumes typed characters before anything else.
        if (deps_.router && deps_.router->is_editing())
        {
            if (deps_.router->rename_text_input(event.text))
            {
                if (log_would_emit(LogLevel::Trace, LogCategory::Input))
                    log_printf(LogLevel::Trace, LogCategory::Input, "input trace: dispatcher text_input consumed by rename_text_input");
                if (deps_.request_frame)
                    deps_.request_frame();
                return;
            }
        }
        // Overlay host consumes text input when active.
        if (deps_.router)
        {
            if (IHost* overlay = deps_.router->overlay_host())
            {
                if (log_would_emit(LogLevel::Trace, LogCategory::Input))
                {
                    const std::string overlay_name = host_trace_name(overlay);
                    log_printf(LogLevel::Trace, LogCategory::Input,
                        "input trace: dispatcher text_input routed to overlay host=%p(%s)",
                        static_cast<void*>(overlay),
                        overlay_name.c_str());
                }
                overlay->on_text_input(event);
                request_imgui_frame_if_needed(deps_);
                return;
            }
        }
        deps_.ui_panel->on_text_input(event);
        request_imgui_frame_if_needed(deps_);
        if (!deps_.ui_panel->wants_keyboard() && deps_.host)
        {
            if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            {
                const std::string host_name = host_trace_name(deps_.host);
                log_printf(LogLevel::Trace, LogCategory::Input,
                    "input trace: dispatcher forwarding text_input to host=%p(%s)",
                    static_cast<void*>(deps_.host),
                    host_name.c_str());
            }
            deps_.host->on_text_input(event);
        }
    };

    callbacks.on_text_editing = [this](const TextEditingEvent& event) {
        // Overlay host consumes IME composition events.
        if (deps_.router && deps_.router->overlay_host())
            return;
        request_imgui_frame_if_needed(deps_);
        if (deps_.host)
            deps_.host->on_text_editing(event);
    };

    callbacks.on_mouse_button = [this](const MouseButtonEvent& e) { on_mouse_button_event(e); };
    callbacks.on_mouse_move = [this](const MouseMoveEvent& e) { on_mouse_move_event(e); };
    callbacks.on_mouse_wheel = [this](const MouseWheelEvent& e) { on_mouse_wheel_event(e); };

    callbacks.on_resize = [this](const WindowResizeEvent& event) {
        if (deps_.on_resize)
            deps_.on_resize(event.size.x, event.size.y);
    };

    callbacks.on_display_scale_changed = [this](const DisplayScaleEvent& event) {
        if (deps_.on_display_scale_changed)
            deps_.on_display_scale_changed(event.display_ppi);
    };

    callbacks.on_drop_file = [this](std::string_view path) {
        if (deps_.host)
            deps_.host->dispatch_action(std::string("open_file:") + std::string(path));
    };
    window_connection_ = window.connect_input_callbacks(std::move(callbacks));
}

InputDispatcher::ChordIndicatorState InputDispatcher::chord_indicator_state(
    std::chrono::steady_clock::time_point now) const
{
    ChordIndicatorState state;
    state.text = indicator_text_;
    if (state.text.empty())
        return state;

    if (fade_started_at_ && fade_ends_at_)
    {
        const auto total = std::chrono::duration_cast<std::chrono::milliseconds>(*fade_ends_at_ - *fade_started_at_);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - *fade_started_at_);
        if (total.count() <= 0)
        {
            state.alpha = 0.0f;
            return state;
        }
        const float t = std::clamp(static_cast<float>(elapsed.count()) / static_cast<float>(total.count()), 0.0f, 1.0f);
        state.alpha = 1.0f - t;
        return state;
    }

    state.alpha = 1.0f;
    return state;
}

bool InputDispatcher::update(std::chrono::steady_clock::time_point now, int chord_timeout_ms)
{
    bool changed = false;

    if (prefix_active_ && prefix_started_at_)
    {
        const auto timeout = std::chrono::milliseconds(std::max(100, chord_timeout_ms));
        if (now - *prefix_started_at_ >= timeout)
        {
            prefix_active_ = false;
            pane_select_active_ = false;
            prefix_started_at_.reset();
            start_indicator_fade(now);
            changed = true;
        }
    }

    if (fade_ends_at_)
    {
        if (now >= *fade_ends_at_)
        {
            indicator_text_.clear();
            fade_started_at_.reset();
            fade_ends_at_.reset();
            changed = true;
        }
        else if (deps_.request_frame)
        {
            deps_.request_frame();
        }
    }

    return changed;
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
