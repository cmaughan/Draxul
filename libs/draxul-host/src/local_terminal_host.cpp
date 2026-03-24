#include <draxul/local_terminal_host.h>

#include <algorithm>
#include <draxul/log.h>
#include <draxul/window.h>

namespace draxul
{

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
        return cbs;
    }())
{
}

// ---------------------------------------------------------------------------
// pump / key / input / action
// ---------------------------------------------------------------------------

void LocalTerminalHost::pump()
{
    auto chunks = do_process_drain();
    if (!chunks.empty())
    {
        if (scrollback_.is_scrolled_back())
            scrollback_.scroll_to_live();
        selection_.clear();

        // Process all available output, re-draining after each batch so that
        // closely-spaced bursts (e.g. fzf exit: cleanup then prompt redraw)
        // are coalesced into a single flush instead of rendering a partial
        // intermediate frame.
        do
        {
            for (const auto& chunk : chunks)
                consume_output(chunk);
            // Re-drain: coalesces bursts that arrived during processing.
            chunks = do_process_drain();
        } while (!chunks.empty());

        flush_grid();
    }
    advance_cursor_blink(std::chrono::steady_clock::now());
}

void LocalTerminalHost::on_key(const KeyEvent& event)
{
    if (event.pressed && scrollback_.is_scrolled_back())
        scrollback_.scroll_to_live();
    TerminalHostBase::on_key(event);
}

void LocalTerminalHost::on_text_input(const TextInputEvent& event)
{
    if (!event.text.empty() && scrollback_.is_scrolled_back())
        scrollback_.scroll_to_live();
    TerminalHostBase::on_text_input(event);
}

bool LocalTerminalHost::dispatch_action(std::string_view action)
{
    if (action == "copy" && selection_.is_active())
    {
        const std::string text = selection_.extract_text();
        if (!text.empty())
            window().set_clipboard_text(text);
        selection_.clear();
        return true;
    }
    if (action == "copy")
        return true;
    if (action == "paste" && scrollback_.is_scrolled_back())
        scrollback_.scroll_to_live();
    return TerminalHostBase::dispatch_action(action);
}

// ---------------------------------------------------------------------------
// Mouse events
// ---------------------------------------------------------------------------

void LocalTerminalHost::on_mouse_button(const MouseButtonEvent& event)
{
    const GridPos pos = pixel_to_cell(event.pos.x, event.pos.y);

    if (mouse_reporter_.on_button(event.button, event.pressed, event.mod, pos.col, pos.row))
        return;

    if (event.button == 1 && event.pressed)
    {
        selection_.begin_drag({ { pos.col, pos.row } });
        return;
    }
    if (event.button == 1)
        selection_.end_drag({ { pos.col, pos.row } });
}

void LocalTerminalHost::on_mouse_move(const MouseMoveEvent& event)
{
    const GridPos pos = pixel_to_cell(event.pos.x, event.pos.y);

    if (mouse_reporter_.on_move(event.mod, pos.col, pos.row))
        return;

    selection_.update_drag({ { pos.col, pos.row } });
}

void LocalTerminalHost::on_mouse_wheel(const MouseWheelEvent& event)
{
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

// ---------------------------------------------------------------------------
// Viewport / state reset
// ---------------------------------------------------------------------------

void LocalTerminalHost::on_viewport_changed()
{
    const int new_cols = std::max(1, viewport().grid_size.x);
    const int new_rows = std::max(1, viewport().grid_size.y);
    if (new_cols == grid_cols() && new_rows == grid_rows())
        return;

    if (new_cols != grid_cols())
        scrollback_.resize(new_cols);
    else if (scrollback_.is_scrolled_back())
        scrollback_.reset();
    selection_.clear();

    TerminalHostBase::on_viewport_changed();
}

void LocalTerminalHost::reset_terminal_state()
{
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
    mouse_reporter_.set_mode(mode, enable);
}

// ---------------------------------------------------------------------------
// Mouse reporting helpers
// ---------------------------------------------------------------------------

LocalTerminalHost::GridPos LocalTerminalHost::pixel_to_cell(int px, int py) const
{
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

} // namespace draxul
