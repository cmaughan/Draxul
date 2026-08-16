#include <draxul/terminal_surface_host_base.h>

#include <SDL3/SDL_keycode.h>

#include <draxul/input_types.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/string_util.h>
#include <draxul/window.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace draxul
{

namespace
{

bool is_selection_copy_shortcut(const KeyEvent& event)
{
    return event.pressed && event.keycode == SDLK_C
        && has_only_modifiers(event.mod, kModCtrl);
}

} // namespace

TerminalSurfaceHostBase::TerminalSurfaceHostBase()
    : mouse_reporter_([this](std::string_view sequence) {
        surface_write_mouse_report(sequence);
    })
    , selection_([this]() -> SelectionManager::Callbacks {
        SelectionManager::Callbacks cbs;
        cbs.set_overlay_cells = [this](std::vector<CellUpdate> cells) {
            set_overlay_cells(cells);
        };
        cbs.get_cell = [this](int col, int row) -> const Cell& {
            return grid().get_cell(col, row);
        };
        cbs.grid_cols = [this] { return grid_cols(); };
        cbs.grid_rows = [this] { return grid_rows(); };
        cbs.request_frame = [this] { callbacks().request_frame(); };
        cbs.on_selection_truncated = [this](std::string_view message) {
            // Union of the two historical behaviours: the local host logged a
            // warning, the remote host raised a toast. Both are useful.
            DRAXUL_LOG_WARN(LogCategory::App, "%.*s",
                static_cast<int>(message.size()), message.data());
            callbacks().push_toast(1, message);
        };
        return cbs;
    }())
{
}

bool TerminalSurfaceHostBase::initialize(
    const HostContext& context, IHostCallbacks& callbacks)
{
    if (!GridHostBase::initialize(context, callbacks))
        return false;
    if (launch_options().selection_max_cells > 0)
        selection_.set_max_cells(launch_options().selection_max_cells);
    return true;
}

void TerminalSurfaceHostBase::on_config_reloaded(const HostReloadConfig& config)
{
    GridHostBase::on_config_reloaded(config);
    launch_options().selection_max_cells = config.selection_max_cells;
    launch_options().copy_on_select = config.copy_on_select;
    if (config.selection_max_cells > 0)
        selection_.set_max_cells(config.selection_max_cells);
}

void TerminalSurfaceHostBase::on_focus_lost()
{
    GridHostBase::on_focus_lost();
    pending_selection_copy_click_.reset();
}

// ---------------------------------------------------------------------------
// Key / text / action surface prefixes
// ---------------------------------------------------------------------------

bool TerminalSurfaceHostBase::handle_surface_key(const KeyEvent& event)
{
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace, LogCategory::Input,
            "input trace: terminal_surface on_key key=%d mod=0x%X pressed=%d selection_active=%d suppress_text=%d copy_mode=%d",
            event.keycode,
            static_cast<unsigned int>(event.mod),
            event.pressed ? 1 : 0,
            selection_.is_active() ? 1 : 0,
            suppress_next_selection_copy_text_input_ ? 1 : 0,
            copy_mode_.active ? 1 : 0);
    }
    if (event.pressed && suppress_next_selection_copy_text_input_)
    {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: terminal_surface clearing stale suppress flag before new key");
        suppress_next_selection_copy_text_input_ = false;
    }

    if (copy_mode_.active)
    {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: terminal_surface swallowing key because copy mode is active");
        // Always swallow keys in copy mode; never forward to the process,
        // whether or not handle_copy_mode_key consumed the binding.
        (void)handle_copy_mode_key(event);
        return true;
    }

    if (selection_.is_active() && is_selection_copy_shortcut(event))
    {
        copy_active_selection_to_clipboard();
        selection_.clear();
        suppress_next_selection_copy_text_input_ = true;
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
        {
            log_printf(LogLevel::Trace, LogCategory::Input,
                "input trace: terminal_surface swallowed Ctrl+C for selection copy, cleared selection, and enabled suppress_next_selection_copy_text_input");
        }
        return true;
    }

    return false;
}

bool TerminalSurfaceHostBase::handle_surface_text_input(const TextInputEvent& event)
{
    if (suppress_next_selection_copy_text_input_)
    {
        suppress_next_selection_copy_text_input_ = false;
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: terminal_surface swallowed follow-up text_input after selection copy");
        return true;
    }
    if (copy_mode_.active)
    {
        // Keys are swallowed in copy mode; swallow the paired text-input
        // events too so printable copy-mode bindings (v, y, hjkl) cannot leak
        // into the terminal process.
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: terminal_surface swallowed text_input while copy mode is active");
        return true;
    }
    (void)event;
    return false;
}

bool TerminalSurfaceHostBase::handle_surface_action(std::string_view action)
{
    if (action == "toggle_copy_mode")
    {
        if (copy_mode_.active)
            exit_copy_mode(false);
        else
            enter_copy_mode();
        return true;
    }
    if (action == "copy")
    {
        copy_active_selection_to_clipboard();
        return true;
    }
    return false;
}

bool TerminalSurfaceHostBase::copy_active_selection_to_clipboard()
{
    PERF_MEASURE();
    if (!selection_.is_active())
    {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: terminal_surface copy_active_selection_to_clipboard aborted because selection is inactive");
        return false;
    }

    const std::string text = selection_.extract_text();
    if (text.empty())
    {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: terminal_surface copy_active_selection_to_clipboard aborted because extracted text is empty");
        return false;
    }
    const bool ok = window().set_clipboard_text(text);
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        const std::string described = describe_text_for_log(text);
        log_printf(LogLevel::Trace, LogCategory::Input,
            "input trace: terminal_surface copied selection to clipboard ok=%d bytes=%zu text=%s",
            ok ? 1 : 0,
            text.size(),
            described.c_str());
    }
    return ok;
}

void TerminalSurfaceHostBase::clear_surface_selection()
{
    selection_.clear();
    pending_selection_copy_click_.reset();
}

// ---------------------------------------------------------------------------
// Mouse events
// ---------------------------------------------------------------------------

void TerminalSurfaceHostBase::on_mouse_button(const MouseButtonEvent& event)
{
    PERF_MEASURE();
    const GridPos pos = pixel_to_cell(event.pos.x, event.pos.y);
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace, LogCategory::Input,
            "input trace: terminal_surface mouse_button button=%d pressed=%d clicks=%d mod=0x%X pixel=(%d,%d) cell=(%d,%d) selection_active=%d pending_copy_click=%d",
            event.button,
            event.pressed ? 1 : 0,
            event.clicks,
            static_cast<unsigned int>(event.mod),
            event.pos.x,
            event.pos.y,
            pos.col,
            pos.row,
            selection_.is_active() ? 1 : 0,
            pending_selection_copy_click_.has_value() ? 1 : 0);
    }

    if (event.button == 1 && event.pressed && open_link_at(pos, event.mod))
        return;

    if (surface_mouse_reporting_allowed()
        && mouse_reporter_.on_button(
            event.button, event.pressed, event.mod, pos.col, pos.row))
    {
        return;
    }

    if (event.button == 1 && event.pressed)
    {
        pending_selection_copy_click_.reset();

        // Double-click: word selection. Triple-click (and beyond): line selection.
        if (event.clicks == 2)
        {
            const bool became_active = selection_.select_word({ { pos.col, pos.row } });
            if (became_active && launch_options().copy_on_select && selection_.is_active())
                copy_active_selection_to_clipboard();
            return;
        }
        if (event.clicks >= 3)
        {
            const bool became_active = selection_.select_line({ { pos.col, pos.row } });
            if (became_active && launch_options().copy_on_select && selection_.is_active())
                copy_active_selection_to_clipboard();
            return;
        }
        if (selection_.is_active() && selection_.contains({ { pos.col, pos.row } }))
        {
            pending_selection_copy_click_ = pos;
            if (log_would_emit(LogLevel::Trace, LogCategory::Input))
                log_printf(LogLevel::Trace, LogCategory::Input, "input trace: terminal_surface armed click-to-copy inside active selection");
            return;
        }
        selection_.begin_drag({ { pos.col, pos.row } });
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: terminal_surface began drag selection");
        return;
    }
    if (event.button == 1)
    {
        if (pending_selection_copy_click_.has_value())
        {
            pending_selection_copy_click_.reset();
            copy_active_selection_to_clipboard();
            selection_.clear();
            if (log_would_emit(LogLevel::Trace, LogCategory::Input))
                log_printf(LogLevel::Trace, LogCategory::Input, "input trace: terminal_surface completed click-to-copy and cleared selection");
            return;
        }
        const bool became_active = selection_.end_drag({ { pos.col, pos.row } });
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
        {
            log_printf(LogLevel::Trace, LogCategory::Input,
                "input trace: terminal_surface ended drag selection became_active=%d selection_active=%d",
                became_active ? 1 : 0,
                selection_.is_active() ? 1 : 0);
        }
        if (became_active && launch_options().copy_on_select && selection_.is_active())
            copy_active_selection_to_clipboard();
    }
}

void TerminalSurfaceHostBase::on_mouse_move(const MouseMoveEvent& event)
{
    PERF_MEASURE();
    const GridPos pos = pixel_to_cell(event.pos.x, event.pos.y);
    if (log_would_emit(LogLevel::Trace, LogCategory::Input)
        && (pending_selection_copy_click_.has_value() || selection_.is_active()))
    {
        log_printf(LogLevel::Trace, LogCategory::Input,
            "input trace: terminal_surface mouse_move pixel=(%d,%d) cell=(%d,%d) selection_active=%d pending_copy_click=%d",
            event.pos.x,
            event.pos.y,
            pos.col,
            pos.row,
            selection_.is_active() ? 1 : 0,
            pending_selection_copy_click_.has_value() ? 1 : 0);
    }

    if (surface_mouse_reporting_allowed()
        && mouse_reporter_.on_move(event.mod, pos.col, pos.row))
    {
        return;
    }

    if (pending_selection_copy_click_.has_value())
    {
        const GridPos anchor = *pending_selection_copy_click_;
        if (anchor.col == pos.col && anchor.row == pos.row)
            return;

        pending_selection_copy_click_.reset();
        selection_.begin_drag({ { anchor.col, anchor.row } });
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: terminal_surface converted pending click-to-copy into a drag selection");
    }

    selection_.update_drag({ { pos.col, pos.row } });
}

void TerminalSurfaceHostBase::on_mouse_wheel(const MouseWheelEvent& event)
{
    PERF_MEASURE();
    if (surface_mouse_reporting_allowed()
        && mouse_reporter_.mode() != MouseReporter::MouseMode::None)
    {
        const GridPos pos = pixel_to_cell(event.pos.x, event.pos.y);
        const int button_code = event.delta.y > 0 ? 64 : 65;
        mouse_reporter_.on_wheel(button_code, event.mod, pos.col, pos.row);
        return;
    }

    const int lines = std::max(1, static_cast<int>(std::abs(event.delta.y) * 3.0f + 0.5f));
    // Scrolling shifts the content underneath the grid-anchored selection, so
    // a later copy would grab text the user never selected. Clear it (the
    // remote host's historical behaviour, adopted for both hosts).
    clear_surface_selection();
    surface_scroll_lines(event.delta.y > 0 ? lines : -lines);
}

std::optional<MouseCursor> TerminalSurfaceHostBase::mouse_cursor_at(int px, int py) const
{
    const GridPos pos = pixel_to_cell(px, py);
    return grid().effective_link_id(pos.col, pos.row) != 0 ? std::optional<MouseCursor>(MouseCursor::Pointer)
                                                           : std::nullopt;
}

void TerminalSurfaceHostBase::set_scroll_offset(float /*px*/)
{
    // Terminal scrollback is line-based. Applying the renderer's fractional
    // smooth-scroll offset on top of discrete scrollback updates causes visible
    // jitter on touchpads, especially on macOS trackpads.
    GridHostBase::set_scroll_offset(0.0f);
}

// ---------------------------------------------------------------------------
// Pixel → cell and hyperlink activation
// ---------------------------------------------------------------------------

TerminalSurfaceHostBase::GridPos TerminalSurfaceHostBase::pixel_to_cell(int px, int py) const
{
    PERF_MEASURE();
    auto [cell_w, cell_h] = renderer().cell_size_pixels();
    const int pad = renderer().padding();
    if (cell_w <= 0)
        cell_w = 1;
    if (cell_h <= 0)
        cell_h = 1;
    const int col = std::clamp(
        (px - viewport().pixel_pos.x - pad) / cell_w, 0, std::max(0, grid_cols() - 1));
    const int row = std::clamp(
        (py - viewport().pixel_pos.y - pad) / cell_h, 0, std::max(0, grid_rows() - 1));
    return { col, row };
}

bool TerminalSurfaceHostBase::open_link_at(const GridPos& pos, ModifierFlags mod)
{
    const uint16_t link_id = grid().effective_link_id(pos.col, pos.row);
    if (link_id == 0)
        return false;

    const bool explicit_link = grid().cell_has_explicit_hyperlink(pos.col, pos.row);
    const bool url_modifier = (mod & kModCtrl) || (mod & kModSuper);
    if (!explicit_link && !url_modifier)
        return false;

    const std::string_view uri = grid().link_uri(link_id);
    if (uri.empty())
        return false;

    if (!window().open_url(uri))
        callbacks().push_toast(2, "Failed to open link.");
    return true;
}

// ---------------------------------------------------------------------------
// Keyboard copy mode (vim/tmux-style)
// ---------------------------------------------------------------------------

void TerminalSurfaceHostBase::enter_copy_mode()
{
    PERF_MEASURE();
    pending_selection_copy_click_.reset();
    suppress_next_selection_copy_text_input_ = false;
    selection_.clear();
    copy_mode_.active = true;
    copy_mode_.selecting = false;
    copy_mode_.line_mode = false;
    // Start the cursor at the current terminal cursor position when possible.
    const glm::ivec2 cursor = surface_cursor_position();
    copy_mode_.cursor = {
        std::clamp(cursor.x, 0, std::max(0, grid_cols() - 1)),
        std::clamp(cursor.y, 0, std::max(0, grid_rows() - 1)),
    };
    copy_mode_.anchor = copy_mode_.cursor;
    update_copy_mode_overlay();
    callbacks().push_toast(0,
        "Copy mode: hjkl/arrows to move, v/V select, y yank, q/Esc exit");
}

void TerminalSurfaceHostBase::exit_copy_mode(bool yank)
{
    PERF_MEASURE();
    if (yank && selection_.is_active())
        copy_active_selection_to_clipboard();
    pending_selection_copy_click_.reset();
    suppress_next_selection_copy_text_input_ = false;
    selection_.clear();
    copy_mode_ = {};
    callbacks().request_frame();
}

void TerminalSurfaceHostBase::update_copy_mode_overlay()
{
    PERF_MEASURE();
    if (copy_mode_.selecting)
    {
        if (copy_mode_.line_mode)
        {
            const int cols = grid_cols();
            const int r1 = std::min(copy_mode_.anchor.y, copy_mode_.cursor.y);
            const int r2 = std::max(copy_mode_.anchor.y, copy_mode_.cursor.y);
            selection_.begin_drag({ { 0, r1 } });
            selection_.end_drag({ { std::max(0, cols - 1), r2 } });
        }
        else
        {
            selection_.begin_drag({ { copy_mode_.anchor.x, copy_mode_.anchor.y } });
            selection_.end_drag({ { copy_mode_.cursor.x, copy_mode_.cursor.y } });
        }
    }
    else
    {
        // Show a single-cell anchor highlight at the cursor.
        selection_.begin_drag({ { copy_mode_.cursor.x, copy_mode_.cursor.y } });
        selection_.end_drag({ { copy_mode_.cursor.x, copy_mode_.cursor.y } });
    }
    callbacks().request_frame();
}

void TerminalSurfaceHostBase::refresh_copy_mode_after_grid_change()
{
    if (!copy_mode_.active)
        return;
    copy_mode_.cursor.x = std::clamp(copy_mode_.cursor.x, 0, std::max(0, grid_cols() - 1));
    copy_mode_.cursor.y = std::clamp(copy_mode_.cursor.y, 0, std::max(0, grid_rows() - 1));
    update_copy_mode_overlay();
}

bool TerminalSurfaceHostBase::handle_copy_mode_key(const KeyEvent& event)
{
    PERF_MEASURE();
    // Swallow key releases without acting on them; only presses navigate.
    if (!event.pressed)
        return true;

    const int cols = grid_cols();
    const int rows = grid_rows();
    auto clamp_cursor = [&]() {
        copy_mode_.cursor.x = std::clamp(copy_mode_.cursor.x, 0, std::max(0, cols - 1));
        copy_mode_.cursor.y = std::clamp(copy_mode_.cursor.y, 0, std::max(0, rows - 1));
    };

    switch (event.keycode)
    {
    case SDLK_ESCAPE:
    case SDLK_Q:
        exit_copy_mode(false);
        return true;
    case SDLK_Y:
        exit_copy_mode(true);
        return true;
    case SDLK_V:
        if ((event.mod & kModShift) != 0)
        {
            copy_mode_.selecting = true;
            copy_mode_.line_mode = true;
            copy_mode_.anchor = copy_mode_.cursor;
        }
        else
        {
            copy_mode_.selecting = !copy_mode_.selecting;
            copy_mode_.line_mode = false;
            if (copy_mode_.selecting)
                copy_mode_.anchor = copy_mode_.cursor;
            else
                selection_.clear();
        }
        update_copy_mode_overlay();
        return true;
    case SDLK_H:
    case SDLK_LEFT:
        --copy_mode_.cursor.x;
        clamp_cursor();
        update_copy_mode_overlay();
        return true;
    case SDLK_L:
    case SDLK_RIGHT:
        ++copy_mode_.cursor.x;
        clamp_cursor();
        update_copy_mode_overlay();
        return true;
    case SDLK_K:
    case SDLK_UP:
        --copy_mode_.cursor.y;
        clamp_cursor();
        update_copy_mode_overlay();
        return true;
    case SDLK_J:
    case SDLK_DOWN:
        ++copy_mode_.cursor.y;
        clamp_cursor();
        update_copy_mode_overlay();
        return true;
    case SDLK_0:
    case SDLK_HOME:
        copy_mode_.cursor.x = 0;
        update_copy_mode_overlay();
        return true;
    case SDLK_END:
        copy_mode_.cursor.x = std::max(0, cols - 1);
        update_copy_mode_overlay();
        return true;
    case SDLK_G:
        if ((event.mod & kModShift) != 0)
            copy_mode_.cursor.y = std::max(0, rows - 1);
        else
            copy_mode_.cursor.y = 0;
        update_copy_mode_overlay();
        return true;
    default:
        return true;
    }
}

} // namespace draxul
