#include <draxul/local_terminal_host.h>

#include <SDL3/SDL_keycode.h>
#include <algorithm>
#include <draxul/input_types.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/string_util.h>
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

} // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

LocalTerminalHost::LocalTerminalHost()
    : scrollback_([this]() -> ScrollbackBuffer::Callbacks {
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
    return true;
}

void LocalTerminalHost::on_config_reloaded(const HostReloadConfig& config)
{
    TerminalHostBase::on_config_reloaded(config);
    launch_options().paste_confirm_lines = config.paste_confirm_lines;
    launch_options().scrollback_lines = config.scrollback_lines;
    scrollback_.set_capacity(config.scrollback_lines);
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
        clear_surface_selection();

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
    if (handle_surface_key(event))
        return;

    // Shift+PageUp/Down/Home/End for scrollback navigation.
    if (event.pressed && (event.mod & kModShift))
    {
        if (event.keycode == SDLK_PAGEUP)
        {
            clear_surface_selection();
            scrollback_.scroll(grid_rows());
            return;
        }
        if (event.keycode == SDLK_PAGEDOWN)
        {
            clear_surface_selection();
            scrollback_.scroll(-grid_rows());
            return;
        }
        if (event.keycode == SDLK_HOME)
        {
            clear_surface_selection();
            scrollback_.scroll(scrollback_.size());
            return;
        }
        if (event.keycode == SDLK_END)
        {
            clear_surface_selection();
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
            "input trace: local_terminal_host on_text_input text=%s len=%zu",
            described.c_str(),
            event.text.size());
    }
    if (handle_surface_text_input(event))
        return;

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
    if (handle_surface_action(action))
        return true;
    if (action == "paste" && scrollback_.is_scrolled_back())
        scrollback_.scroll_to_live();
    return TerminalHostBase::dispatch_action(action);
}

// ---------------------------------------------------------------------------
// Surface seams
// ---------------------------------------------------------------------------

void LocalTerminalHost::surface_scroll_lines(int rows)
{
    scrollback_.scroll(rows);
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
    clear_surface_selection();

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
    mouse_reporter().reset();
    scrollback_.reset();
    clear_surface_selection();
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
    mouse_reporter().set_mode(mode, enable);
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

} // namespace draxul
