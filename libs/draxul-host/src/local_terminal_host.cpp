#include <draxul/local_terminal_host.h>

#include <SDL3/SDL_keycode.h>
#include <algorithm>
#include <draxul/input_types.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/window.h>

namespace draxul
{

namespace
{

struct GridSnapshot
{
    int cols = 0;
    int rows = 0;
    std::vector<Cell> cells;

    [[nodiscard]] bool empty() const
    {
        return cols <= 0 || rows <= 0 || cells.empty();
    }
};

GridSnapshot capture_grid_snapshot(const Grid& grid, int cols, int rows)
{
    PERF_MEASURE();
    GridSnapshot snapshot;
    snapshot.cols = cols;
    snapshot.rows = rows;
    snapshot.cells.reserve(static_cast<size_t>(cols) * static_cast<size_t>(rows));
    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
            snapshot.cells.push_back(grid.get_cell(col, row));
    }
    return snapshot;
}

void restore_grid_snapshot(Grid& grid, int dst_cols, int dst_rows, const GridSnapshot& snapshot)
{
    PERF_MEASURE();
    if (snapshot.empty())
        return;

    const int copy_cols = std::min(snapshot.cols, dst_cols);
    const int copy_rows = std::min(snapshot.rows, dst_rows);
    const int src_row_offset = snapshot.rows - copy_rows;
    const int dst_row_offset = dst_rows - copy_rows;

    for (int row = 0; row < copy_rows; ++row)
    {
        for (int col = 0; col < copy_cols; ++col)
        {
            const auto& cell = snapshot.cells[static_cast<size_t>(src_row_offset + row) * snapshot.cols + col];
            if (cell.double_width_cont)
                continue;
            if (cell.double_width && col + 1 >= copy_cols)
                continue;
            grid.set_cell(col, dst_row_offset + row, std::string(cell.text.view()), cell.hl_attr_id, cell.double_width);
        }
    }
}

bool is_selection_copy_shortcut(const KeyEvent& event)
{
    if (!event.pressed || event.keycode != SDLK_C)
        return false;
    ModifierFlags normalized_mods = kModNone;
    if (event.mod & kModShift)
        normalized_mods |= kModShift;
    if (event.mod & kModCtrl)
        normalized_mods |= kModCtrl;
    if (event.mod & kModAlt)
        normalized_mods |= kModAlt;
    if (event.mod & kModSuper)
        normalized_mods |= kModSuper;
    return normalized_mods == kModCtrl;
}

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

} // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

LocalTerminalHost::LocalTerminalHost()
    : mouse_reporter_([this](std::string_view seq) { do_process_write(seq); })
    , scrollback_([this]() -> ScrollbackBuffer::Callbacks {
        ScrollbackBuffer::Callbacks cbs;
        cbs.grid_cols = [this]() { return grid_cols(); };
        cbs.grid_rows = [this]() { return grid_rows(); };
        cbs.get_cell = [this](int col, int row) { return grid().get_cell(col, row); };
        cbs.set_cell = [this](int col, int row, const Cell& c) {
            grid().set_cell(col, row, std::string(c.text.view()), c.hl_attr_id, c.double_width);
        };
        cbs.force_full_redraw = [this]() { force_full_redraw(); };
        cbs.flush_grid = [this]() { flush_grid(); };
        return cbs;
    }())
    , selection_([this]() -> SelectionManager::Callbacks {
        SelectionManager::Callbacks cbs;
        cbs.set_overlay_cells = [this](std::vector<CellUpdate> cells) {
            set_overlay_cells(cells);
        };
        cbs.get_cell = [this](int col, int row) -> const Cell& {
            return grid().get_cell(col, row);
        };
        cbs.grid_cols = [this]() { return grid_cols(); };
        cbs.grid_rows = [this]() { return grid_rows(); };
        cbs.request_frame = [this]() { callbacks().request_frame(); };
        cbs.on_selection_truncated = [](std::string_view msg) {
            DRAXUL_LOG_WARN(LogCategory::App, "%.*s",
                static_cast<int>(msg.size()), msg.data());
        };
        return cbs;
    }())
{
}

// ---------------------------------------------------------------------------
// initialize / config reload
// ---------------------------------------------------------------------------

bool LocalTerminalHost::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    if (!TerminalHostBase::initialize(context, callbacks))
        return false;
    scrollback_.set_capacity(launch_options().scrollback_lines);
    if (launch_options().selection_max_cells > 0)
        selection_.set_max_cells(launch_options().selection_max_cells);
    return true;
}

void LocalTerminalHost::on_config_reloaded(const HostReloadConfig& config)
{
    TerminalHostBase::on_config_reloaded(config);
    launch_options().selection_max_cells = config.selection_max_cells;
    launch_options().copy_on_select = config.copy_on_select;
    launch_options().paste_confirm_lines = config.paste_confirm_lines;
    launch_options().scrollback_lines = config.scrollback_lines;
    scrollback_.set_capacity(config.scrollback_lines);
    if (config.selection_max_cells > 0)
        selection_.set_max_cells(config.selection_max_cells);
}

// ---------------------------------------------------------------------------
// pump / key / input / action
// ---------------------------------------------------------------------------

void LocalTerminalHost::pump()
{
    PERF_MEASURE();
    ensure_pty_capture_ready();
    auto chunks = do_process_drain();
    const bool saw_output = !chunks.empty();
    if (!chunks.empty())
    {
        ++agent_output_generation_;
        agent_last_output_at_ = std::chrono::steady_clock::now();

        // Don't snap to live view on output — only on user input (handled
        // in on_key/on_text_input). This lets the user scroll back while
        // a program is producing output (e.g. `while true; do date; sleep 1; done`).
        selection_.clear();

        // Process all available output, re-draining after each batch so that
        // closely-spaced bursts (e.g. fzf exit: cleanup then prompt redraw)
        // are coalesced into a single flush instead of rendering a partial
        // intermediate frame.
        begin_output_cursor_batch();
        do
        {
            for (const auto& chunk : chunks)
            {
                maybe_capture_pty_chunk(chunk);
                consume_output(chunk);
            }
            // Re-drain: coalesces bursts that arrived during processing.
            chunks = do_process_drain();
        } while (!chunks.empty());
        end_output_cursor_batch();

        // After a resize, the shell clears and redraws — but it may leave
        // rows blank that had content before (e.g. previous prompts that ZLE
        // doesn't own). Restore those rows from the pre-resize snapshot,
        // mimicking tmux's virtual screen buffer behavior.
        if (resize_snapshot_.active)
        {
            resize_snapshot_.active = false;
            const int rows = std::min(resize_snapshot_.rows, grid_rows());
            const int cols = std::min(resize_snapshot_.cols, grid_cols());
            for (int r = 0; r < rows; ++r)
            {
                // Check if this row is blank in the current grid.
                bool current_blank = true;
                for (int c = 0; c < grid_cols(); ++c)
                {
                    const auto& cell = grid().get_cell(c, r);
                    if (cell.hl_attr_id != 0
                        || (!cell.text.empty() && cell.text.view() != " "))
                    {
                        current_blank = false;
                        break;
                    }
                }
                if (!current_blank)
                    continue;

                // Check if the snapshot row had content.
                bool snap_has_content = false;
                const auto snap_offset = static_cast<size_t>(r) * resize_snapshot_.cols;
                for (int c = 0; c < cols; ++c)
                {
                    const auto& cell = resize_snapshot_.cells[snap_offset + c];
                    if (cell.hl_attr_id != 0
                        || (!cell.text.empty() && cell.text.view() != " "))
                    {
                        snap_has_content = true;
                        break;
                    }
                }
                if (!snap_has_content)
                    continue;

                // Restore the row from snapshot.
                for (int c = 0; c < cols; ++c)
                {
                    const auto& src = resize_snapshot_.cells[snap_offset + c];
                    grid().set_cell(c, r, std::string(src.text.view()), src.hl_attr_id, src.double_width);
                }
            }
        }

        // When scrolled back, don't flush the live grid to the renderer —
        // the scrollback display is a static composite and flushing would
        // cause visible stepping as new lines arrive at the bottom.
        if (!scrollback_.is_scrolled_back() && !synchronized_output_active())
            flush_grid();
    }
    reconcile_provisional_cursor_after_pump(saw_output);
    trace_cursor_presentation_state("local_pump_end", saw_output);
    advance_cursor_blink(std::chrono::steady_clock::now());
}

void LocalTerminalHost::on_key(const KeyEvent& event)
{
    PERF_MEASURE();
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace, LogCategory::Input,
            "input trace: local_terminal_host on_key key=%d mod=0x%X pressed=%d selection_active=%d suppress_text=%d copy_mode=%d",
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
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: local_terminal_host clearing stale suppress flag before new key");
        suppress_next_selection_copy_text_input_ = false;
    }

    if (copy_mode_.active)
    {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: local_terminal_host swallowing key because copy mode is active");
        // Always swallow keys in copy mode; never forward to the process,
        // whether or not handle_copy_mode_key consumed the binding.
        (void)handle_copy_mode_key(event);
        return;
    }

    if (selection_.is_active() && is_selection_copy_shortcut(event))
    {
        copy_active_selection_to_clipboard();
        selection_.clear();
        suppress_next_selection_copy_text_input_ = true;
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
        {
            log_printf(LogLevel::Trace, LogCategory::Input,
                "input trace: local_terminal_host swallowed Ctrl+C for selection copy, cleared selection, and enabled suppress_next_selection_copy_text_input");
        }
        return;
    }

    // Shift+PageUp/Down/Home/End for scrollback navigation.
    if (event.pressed && (event.mod & kModShift))
    {
        if (event.keycode == SDLK_PAGEUP)
        {
            scrollback_.scroll(grid_rows());
            return;
        }
        if (event.keycode == SDLK_PAGEDOWN)
        {
            scrollback_.scroll(-grid_rows());
            return;
        }
        if (event.keycode == SDLK_HOME)
        {
            scrollback_.scroll(scrollback_.size());
            return;
        }
        if (event.keycode == SDLK_END)
        {
            scrollback_.scroll_to_live();
            return;
        }
    }

    if (event.pressed && scrollback_.is_scrolled_back())
        scrollback_.scroll_to_live();
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
        log_printf(LogLevel::Trace, LogCategory::Input, "input trace: local_terminal_host forwarding key to TerminalHostBase");
    TerminalHostBase::on_key(event);
}

void LocalTerminalHost::on_text_input(const TextInputEvent& event)
{
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        const std::string described = describe_text_for_log(event.text);
        log_printf(LogLevel::Trace, LogCategory::Input,
            "input trace: local_terminal_host on_text_input text=%s len=%zu selection_active=%d suppress_text=%d",
            described.c_str(),
            event.text.size(),
            selection_.is_active() ? 1 : 0,
            suppress_next_selection_copy_text_input_ ? 1 : 0);
    }
    if (suppress_next_selection_copy_text_input_)
    {
        suppress_next_selection_copy_text_input_ = false;
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: local_terminal_host swallowed follow-up text_input after selection copy");
        return;
    }

    PERF_MEASURE();
    if (!event.text.empty() && scrollback_.is_scrolled_back())
        scrollback_.scroll_to_live();
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
        log_printf(LogLevel::Trace, LogCategory::Input, "input trace: local_terminal_host forwarding text_input to TerminalHostBase");
    TerminalHostBase::on_text_input(event);
}

bool LocalTerminalHost::dispatch_action(std::string_view action)
{
    PERF_MEASURE();
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
    if (action == "paste" && scrollback_.is_scrolled_back())
        scrollback_.scroll_to_live();
    return TerminalHostBase::dispatch_action(action);
}

bool LocalTerminalHost::copy_active_selection_to_clipboard()
{
    PERF_MEASURE();
    if (!selection_.is_active())
    {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: local_terminal_host copy_active_selection_to_clipboard aborted because selection is inactive");
        return false;
    }

    const std::string text = selection_.extract_text();
    if (text.empty())
    {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: local_terminal_host copy_active_selection_to_clipboard aborted because extracted text is empty");
        return false;
    }
    const bool ok = window().set_clipboard_text(text);
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        const std::string described = describe_text_for_log(text);
        log_printf(LogLevel::Trace, LogCategory::Input,
            "input trace: local_terminal_host copied selection to clipboard ok=%d bytes=%zu text=%s",
            ok ? 1 : 0,
            text.size(),
            described.c_str());
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Mouse events
// ---------------------------------------------------------------------------

void LocalTerminalHost::on_mouse_button(const MouseButtonEvent& event)
{
    PERF_MEASURE();
    const GridPos pos = pixel_to_cell(event.pos.x, event.pos.y);
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace, LogCategory::Input,
            "input trace: local_terminal_host mouse_button button=%d pressed=%d clicks=%d mod=0x%X pixel=(%d,%d) cell=(%d,%d) selection_active=%d pending_copy_click=%d",
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

    if (mouse_reporter_.on_button(event.button, event.pressed, event.mod, pos.col, pos.row))
        return;

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
                log_printf(LogLevel::Trace, LogCategory::Input, "input trace: local_terminal_host armed click-to-copy inside active selection");
            return;
        }
        selection_.begin_drag({ { pos.col, pos.row } });
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: local_terminal_host began drag selection");
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
                log_printf(LogLevel::Trace, LogCategory::Input, "input trace: local_terminal_host completed click-to-copy and cleared selection");
            return;
        }
        const bool became_active = selection_.end_drag({ { pos.col, pos.row } });
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
        {
            log_printf(LogLevel::Trace, LogCategory::Input,
                "input trace: local_terminal_host ended drag selection became_active=%d selection_active=%d",
                became_active ? 1 : 0,
                selection_.is_active() ? 1 : 0);
        }
        if (became_active && launch_options().copy_on_select && selection_.is_active())
            copy_active_selection_to_clipboard();
    }
}

void LocalTerminalHost::on_mouse_move(const MouseMoveEvent& event)
{
    PERF_MEASURE();
    const GridPos pos = pixel_to_cell(event.pos.x, event.pos.y);
    if (log_would_emit(LogLevel::Trace, LogCategory::Input)
        && (pending_selection_copy_click_.has_value() || selection_.is_active()))
    {
        log_printf(LogLevel::Trace, LogCategory::Input,
            "input trace: local_terminal_host mouse_move pixel=(%d,%d) cell=(%d,%d) selection_active=%d pending_copy_click=%d",
            event.pos.x,
            event.pos.y,
            pos.col,
            pos.row,
            selection_.is_active() ? 1 : 0,
            pending_selection_copy_click_.has_value() ? 1 : 0);
    }

    if (mouse_reporter_.on_move(event.mod, pos.col, pos.row))
        return;

    if (pending_selection_copy_click_.has_value())
    {
        const GridPos anchor = *pending_selection_copy_click_;
        if (anchor.col == pos.col && anchor.row == pos.row)
            return;

        pending_selection_copy_click_.reset();
        selection_.begin_drag({ { anchor.col, anchor.row } });
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "input trace: local_terminal_host converted pending click-to-copy into a drag selection");
    }

    selection_.update_drag({ { pos.col, pos.row } });
}

void LocalTerminalHost::on_mouse_wheel(const MouseWheelEvent& event)
{
    PERF_MEASURE();
    if (mouse_reporter_.mode() != MouseReporter::MouseMode::None)
    {
        const GridPos pos = pixel_to_cell(event.pos.x, event.pos.y);
        const int button_code = event.delta.y > 0 ? 64 : 65;
        mouse_reporter_.on_wheel(button_code, event.mod, pos.col, pos.row);
        return;
    }

    const int lines = std::max(1, static_cast<int>(std::abs(event.delta.y) * 3.0f + 0.5f));
    scrollback_.scroll(event.delta.y > 0 ? lines : -lines);
}

std::optional<MouseCursor> LocalTerminalHost::mouse_cursor_at(int px, int py) const
{
    const GridPos pos = pixel_to_cell(px, py);
    return grid().effective_link_id(pos.col, pos.row) != 0 ? std::optional<MouseCursor>(MouseCursor::Pointer)
                                                           : std::nullopt;
}

void LocalTerminalHost::set_scroll_offset(float /*px*/)
{
    // Shell-host scrollback is line-based. Applying the renderer's fractional
    // smooth-scroll offset on top of discrete scrollback updates causes visible
    // jitter on touchpads, especially on macOS trackpads.
    GridHostBase::set_scroll_offset(0.0f);
}

// ---------------------------------------------------------------------------
// Viewport / state reset
// ---------------------------------------------------------------------------

std::string LocalTerminalHost::status_text() const
{
    std::string result(host_name());
    if (!is_running())
        result += " [exited]";
    const std::string& cwd = current_working_directory();
    if (!cwd.empty())
    {
        result += " | ";
        constexpr size_t kMaxCwdLen = 30;
        if (cwd.size() > kMaxCwdLen)
        {
            result += "…";
            result += cwd.substr(cwd.size() - (kMaxCwdLen - 1));
        }
        else
        {
            result += cwd;
        }
    }
    if (scrollback_.is_scrolled_back())
    {
        result += " [";
        result += std::to_string(scrollback_.offset());
        result += "/";
        result += std::to_string(scrollback_.size());
        result += "]";
    }
    return result;
}

std::optional<AgentObservation> LocalTerminalHost::capture_agent_observation(
    int max_rows, size_t max_bytes) const
{
    AgentObservation observation;
    observation.output_generation = agent_output_generation_;
    observation.captured_at = std::chrono::steady_clock::now();
    observation.last_output_at = agent_last_output_at_;
    observation.terminal_title = terminal_title();
    observation.cursor_visible = vt_state().cursor_visible;
    observation.cursor_col = cursor_col();
    observation.cursor_row = cursor_row();
    observation.process_running = is_running();
    observation.exit_code = exit_code();

    if (max_rows <= 0 || max_bytes == 0 || grid_cols() <= 0 || grid_rows() <= 0)
        return observation;

    const int first_row = std::max(0, grid_rows() - max_rows);
    size_t remaining = max_bytes;
    observation.bottom_rows.reserve(static_cast<size_t>(grid_rows() - first_row));
    for (int row = first_row; row < grid_rows() && remaining > 0; ++row)
    {
        std::string text;
        for (int col = 0; col < grid_cols(); ++col)
        {
            const Cell& cell = grid().get_cell(col, row);
            if (cell.double_width_cont)
                continue;
            const std::string_view cluster = cell.text.view();
            if (cluster.size() > remaining)
                break;
            text.append(cluster);
            remaining -= cluster.size();
        }
        while (!text.empty() && text.back() == ' ')
            text.pop_back();
        observation.bottom_rows.push_back(std::move(text));
    }
    return observation;
}

bool LocalTerminalHost::send_agent_input(std::string_view bytes)
{
    return is_running() && do_process_write(bytes);
}

void LocalTerminalHost::on_viewport_changed()
{
    PERF_MEASURE();
    const int old_cols = grid_cols();
    const int old_rows = grid_rows();
    const int new_cols = std::max(1, viewport().grid_size.x);
    const int new_rows = std::max(1, viewport().grid_size.y);
    if (new_cols == old_cols && new_rows == old_rows)
        return;

    // Capture the visible grid before anything changes.
    const GridSnapshot visible_snapshot = capture_grid_snapshot(grid(), old_cols, old_rows);

    if (new_cols != old_cols)
        scrollback_.resize(new_cols);
    if (scrollback_.is_scrolled_back())
        scrollback_.scroll_to_live();
    selection_.clear();

    // Phase 2 reflow: when shrinking vertically, push non-blank excess top
    // rows into scrollback so they survive the shell's post-SIGWINCH redraw.
    // Only push rows that contain visible content (non-space text OR non-default
    // highlight) — blank rows would pollute scrollback with empty space.
    // Stop at the first fully-blank row to avoid gaps.
    if (new_rows < old_rows)
    {
        const int excess = old_rows - new_rows;
        for (int r = 0; r < excess; ++r)
        {
            const auto row_offset = static_cast<size_t>(r) * old_cols;
            if (row_offset + old_cols > visible_snapshot.cells.size())
                break;
            bool has_content = false;
            for (int c = 0; c < old_cols; ++c)
            {
                const auto& cell = visible_snapshot.cells[row_offset + c];
                if (cell.hl_attr_id != 0
                    || (!cell.text.empty() && cell.text.view() != " "))
                {
                    has_content = true;
                    break;
                }
            }
            if (!has_content)
                break; // Stop at first blank row — no content below here matters.
            scrollback_.push_row(&visible_snapshot.cells[row_offset], old_cols);
        }
    }

    // Save a resize snapshot so pump() can restore rows the shell blanks.
    // Use the snapshot BEFORE resize (rows that fit in the new grid).
    {
        const int snap_rows = std::min(old_rows, new_rows);
        const int snap_cols = std::min(old_cols, new_cols);
        resize_snapshot_.cells.resize(static_cast<size_t>(snap_cols) * snap_rows);
        resize_snapshot_.cols = snap_cols;
        resize_snapshot_.rows = snap_rows;
        for (int r = 0; r < snap_rows; ++r)
            for (int c = 0; c < snap_cols; ++c)
                resize_snapshot_.cells[static_cast<size_t>(r) * snap_cols + c]
                    = visible_snapshot.cells[static_cast<size_t>(r) * old_cols + c];
        resize_snapshot_.active = true;
    }

    TerminalHostBase::on_viewport_changed();

    // When growing vertically, pull rows back from scrollback into the top of
    // the grid and shift existing content down.
    if (new_rows > old_rows && scrollback_.size() > 0)
    {
        const int pull = std::min(new_rows - old_rows, scrollback_.size());
        // Collect pulled rows (pop_newest_rows visits newest-first, so reverse).
        std::vector<std::vector<Cell>> pulled_rows;
        pulled_rows.reserve(pull);
        scrollback_.pop_newest_rows(pull, [&](std::span<const Cell> row) {
            pulled_rows.emplace_back(row.begin(), row.end());
        });
        std::reverse(pulled_rows.begin(), pulled_rows.end());

        // Shift existing grid content down by `pull` rows.
        for (int r = grid_rows() - 1; r >= pull; --r)
        {
            for (int c = 0; c < grid_cols(); ++c)
            {
                const auto& src = grid().get_cell(c, r - pull);
                grid().set_cell(c, r, std::string(src.text.view()), src.hl_attr_id, src.double_width);
            }
        }

        // Write pulled scrollback rows at the top.
        const int copy_cols = std::min(scrollback_.cols(), grid_cols());
        for (int r = 0; r < pull; ++r)
        {
            for (int c = 0; c < copy_cols; ++c)
            {
                const auto& src = pulled_rows[r][c];
                grid().set_cell(c, r, std::string(src.text.view()), src.hl_attr_id, src.double_width);
            }
            for (int c = copy_cols; c < grid_cols(); ++c)
                grid().set_cell(c, r, " ", 0, false);
        }

        // Adjust cursor position: it shifted down by `pull` rows.
        const int new_cursor_row = std::min(vt_state().row + pull, grid_rows() - 1);
        set_logical_cursor_position(vt_state().col, new_cursor_row);
    }
    else
    {
        // No scrollback pull — restore the visible snapshot (content that fits).
        restore_grid_snapshot(grid(), grid_cols(), grid_rows(), visible_snapshot);
    }

    force_full_redraw();
    flush_grid();
}

void LocalTerminalHost::reset_terminal_state()
{
    PERF_MEASURE();
    TerminalHostBase::reset_terminal_state();
    mouse_reporter_.reset();
    scrollback_.reset();
    selection_.clear();
}

// ---------------------------------------------------------------------------
// Scrollback hook
// ---------------------------------------------------------------------------

void LocalTerminalHost::on_line_scrolled_off(int row)
{
    PERF_MEASURE();
    Cell* slot = scrollback_.next_write_slot();
    if (slot)
    {
        const int cols = grid_cols();
        for (int col = 0; col < cols; ++col)
            slot[col] = grid().get_cell(col, row);
        scrollback_.commit_push();
    }
}

// ---------------------------------------------------------------------------
// Mouse mode hook
// ---------------------------------------------------------------------------

void LocalTerminalHost::on_mouse_mode_changed(int mode, bool enable)
{
    PERF_MEASURE();
    mouse_reporter_.set_mode(mode, enable);
}

// ---------------------------------------------------------------------------
// Mouse reporting helpers
// ---------------------------------------------------------------------------

LocalTerminalHost::GridPos LocalTerminalHost::pixel_to_cell(int px, int py) const
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

bool LocalTerminalHost::open_link_at(const GridPos& pos, ModifierFlags mod)
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
// Highlight compaction hooks — include scrollback in attr collection & remapping
// ---------------------------------------------------------------------------

void LocalTerminalHost::collect_extra_attr_ids(std::unordered_map<uint16_t, HlAttr>& active_attrs)
{
    scrollback_.for_each_cell([&active_attrs, this](const Cell& cell) {
        if (cell.hl_attr_id == 0)
            return;
        active_attrs.try_emplace(cell.hl_attr_id, highlights().get(cell.hl_attr_id));
    });
}

void LocalTerminalHost::remap_extra_highlight_ids(const std::function<uint16_t(uint16_t)>& remap_fn)
{
    scrollback_.remap_highlight_ids(remap_fn);
}

// ---------------------------------------------------------------------------
// Keyboard copy mode (vim/tmux-style)
// ---------------------------------------------------------------------------

void LocalTerminalHost::enter_copy_mode()
{
    PERF_MEASURE();
    pending_selection_copy_click_.reset();
    suppress_next_selection_copy_text_input_ = false;
    selection_.clear();
    copy_mode_.active = true;
    copy_mode_.selecting = false;
    copy_mode_.line_mode = false;
    // Start the cursor at the current terminal cursor position when possible.
    const int cols = grid_cols();
    const int rows = grid_rows();
    int start_col = std::clamp(vt_state().col, 0, std::max(0, cols - 1));
    int start_row = std::clamp(vt_state().row, 0, std::max(0, rows - 1));
    copy_mode_.cursor = { start_col, start_row };
    copy_mode_.anchor = copy_mode_.cursor;
    update_copy_mode_overlay();
    callbacks().push_toast(0,
        "Copy mode: hjkl/arrows to move, v/V select, y yank, q/Esc exit");
}

void LocalTerminalHost::exit_copy_mode(bool yank)
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

void LocalTerminalHost::update_copy_mode_overlay()
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

bool LocalTerminalHost::handle_copy_mode_key(const KeyEvent& event)
{
    PERF_MEASURE();
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
