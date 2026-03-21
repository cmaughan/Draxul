#include <draxul/terminal_host_base.h>

#include <draxul/terminal_key_encoder.h>
#include <draxul/terminal_sgr.h>

#include <algorithm>
#include <array>
#include <draxul/alt_screen_manager.h>
#include <draxul/log.h>
#include <draxul/mouse_reporter.h>
#include <draxul/scrollback_buffer.h>
#include <draxul/selection_manager.h>
#include <draxul/unicode.h>
#include <draxul/vt_parser.h>
#include <draxul/window.h>

namespace draxul
{

namespace
{

Color ansi_color(int index)
{
    static const std::array<Color, 16> palette = { {
        { 0.05f, 0.06f, 0.07f, 1.0f },
        { 0.80f, 0.24f, 0.24f, 1.0f },
        { 0.40f, 0.73f, 0.42f, 1.0f },
        { 0.88f, 0.73f, 0.30f, 1.0f },
        { 0.29f, 0.51f, 0.82f, 1.0f },
        { 0.70f, 0.41f, 0.78f, 1.0f },
        { 0.28f, 0.73f, 0.80f, 1.0f },
        { 0.84f, 0.84f, 0.85f, 1.0f },
        { 0.33f, 0.34f, 0.35f, 1.0f },
        { 0.94f, 0.38f, 0.38f, 1.0f },
        { 0.49f, 0.82f, 0.54f, 1.0f },
        { 0.96f, 0.82f, 0.44f, 1.0f },
        { 0.46f, 0.65f, 0.93f, 1.0f },
        { 0.81f, 0.55f, 0.88f, 1.0f },
        { 0.48f, 0.86f, 0.93f, 1.0f },
        { 0.97f, 0.98f, 0.98f, 1.0f },
    } };
    return palette[std::clamp(index, 0, 15)];
}

} // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

TerminalHostBase::TerminalHostBase()
    : vt_parser_([this]() -> VtParser::Callbacks {
        VtParser::Callbacks cbs;
        cbs.on_cluster = [this](const std::string& cluster) { write_cluster(cluster); };
        cbs.on_control = [this](char ch) { handle_control(ch); };
        cbs.on_esc = [this](char ch) { handle_esc(ch); };
        cbs.on_csi = [this](char final_char, std::string_view body) {
            handle_csi(final_char, body);
        };
        cbs.on_osc = [this](std::string_view body) { handle_osc(body); };
        return cbs;
    }())
    , alt_screen_([this]() -> AltScreenManager::GridAccessors {
        AltScreenManager::GridAccessors acc;
        acc.grid_cols = [this]() { return grid_cols(); };
        acc.grid_rows = [this]() { return grid_rows(); };
        acc.get_cell = [this](int col, int row) { return grid().get_cell(col, row); };
        acc.set_cell = [this](int col, int row, const Cell& c) {
            grid().set_cell(col, row, std::string(c.text.view()), c.hl_attr_id, c.double_width);
        };
        acc.clear_grid = [this]() { grid().clear(); };
        return acc;
    }())
    , mouse_reporter_([this](std::string_view seq) { do_process_write(seq); })
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
            renderer().set_overlay_cells(cells);
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

void TerminalHostBase::pump()
{
    auto chunks = do_process_drain();
    if (!chunks.empty())
    {
        if (scrollback_.is_scrolled_back())
            scrollback_.scroll_to_live();
        selection_.clear();
        for (const auto& chunk : chunks)
            consume_output(chunk);
        flush_grid();
    }
    advance_cursor_blink(std::chrono::steady_clock::now());
}

void TerminalHostBase::on_key(const KeyEvent& event)
{
    if (!event.pressed)
        return;
    if (scrollback_.is_scrolled_back())
        scrollback_.scroll_to_live();
    const std::string sequence = encode_terminal_key(event, vt_);
    if (!sequence.empty())
        do_process_write(sequence);
}

void TerminalHostBase::on_text_input(const TextInputEvent& event)
{
    if (event.text && *event.text)
    {
        if (scrollback_.is_scrolled_back())
            scrollback_.scroll_to_live();
        do_process_write(event.text);
    }
}

bool TerminalHostBase::dispatch_action(std::string_view action)
{
    if (action == "paste")
    {
        if (scrollback_.is_scrolled_back())
            scrollback_.scroll_to_live();
        const std::string clip = window().clipboard_text();
        if (bracketed_paste_mode_)
        {
            std::string wrapped;
            wrapped.reserve(clip.size() + 12);
            wrapped += "\x1B[200~";
            wrapped += clip;
            wrapped += "\x1B[201~";
            do_process_write(wrapped);
        }
        else
        {
            do_process_write(clip);
        }
        return true;
    }
    if (action == "copy")
    {
        if (selection_.is_active())
        {
            const std::string text = selection_.extract_text();
            if (!text.empty())
                window().set_clipboard_text(text);
            selection_.clear();
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Mouse events
// ---------------------------------------------------------------------------

void TerminalHostBase::on_mouse_button(const MouseButtonEvent& event)
{
    const GridPos pos = pixel_to_cell(event.x, event.y);

    if (mouse_reporter_.on_button(event.button, event.pressed, event.mod, pos.col, pos.row))
        return;

    if (event.button == 1)
    {
        if (event.pressed)
        {
            selection_.begin_drag({ pos.col, pos.row });
        }
        else
        {
            selection_.end_drag({ pos.col, pos.row });
        }
    }
}

void TerminalHostBase::on_mouse_move(const MouseMoveEvent& event)
{
    const GridPos pos = pixel_to_cell(event.x, event.y);

    if (mouse_reporter_.on_move(pos.col, pos.row))
        return;

    selection_.update_drag({ pos.col, pos.row });
}

void TerminalHostBase::on_mouse_wheel(const MouseWheelEvent& event)
{
    if (mouse_reporter_.mode() != MouseReporter::MouseMode::None)
    {
        const GridPos pos = pixel_to_cell(event.x, event.y);
        const int button_code = event.dy > 0 ? 64 : 65;
        mouse_reporter_.on_wheel(button_code, pos.col, pos.row);
        return;
    }

    const int lines = std::max(1, static_cast<int>(std::abs(event.dy) * 3.0f + 0.5f));
    scrollback_.scroll(event.dy > 0 ? lines : -lines);
}

// ---------------------------------------------------------------------------
// Viewport / font changes
// ---------------------------------------------------------------------------

void TerminalHostBase::on_viewport_changed()
{
    const int new_cols = std::max(1, viewport().cols);
    const int new_rows = std::max(1, viewport().rows);
    if (new_cols == grid_cols() && new_rows == grid_rows())
        return;

    if (new_cols != grid_cols())
        scrollback_.resize(new_cols);
    else if (scrollback_.is_scrolled_back())
        scrollback_.reset();
    selection_.clear();

    // If we are in alt-screen mode the saved main-screen snapshot must be
    // re-dimensioned to match the new terminal size so that restoring it on
    // alt-screen exit does not produce misaligned content.
    if (alt_screen_.in_alt_screen())
    {
        // The snapshot was captured at the old grid dimensions (grid_cols() x grid_rows()),
        // which are still valid here before apply_grid_size() is called.
        alt_screen_.resize_snapshot(new_cols, new_rows, grid_cols(), grid_rows());
        alt_screen_.clamp_saved_cursor(std::max(0, new_cols - 1), std::max(0, new_rows - 1));
    }

    apply_grid_size(new_cols, new_rows);
    vt_.col = std::clamp(vt_.col, 0, std::max(0, grid_cols() - 1));
    vt_.row = std::clamp(vt_.row, 0, std::max(0, grid_rows() - 1));
    vt_.saved_col = std::clamp(vt_.saved_col, 0, std::max(0, grid_cols() - 1));
    vt_.saved_row = std::clamp(vt_.saved_row, 0, std::max(0, grid_rows() - 1));
    vt_.scroll_top = 0;
    vt_.scroll_bottom = grid_rows() - 1;
    do_process_resize(new_cols, new_rows);
    force_full_redraw();
    update_cursor_style();
}

void TerminalHostBase::on_font_metrics_changed_impl()
{
    force_full_redraw();
    flush_grid();
}

// ---------------------------------------------------------------------------
// Terminal state reset
// ---------------------------------------------------------------------------

void TerminalHostBase::reset_terminal_state()
{
    current_attr_ = {};
    attr_cache_.clear();
    next_attr_id_ = 1;
    vt_parser_.reset();
    vt_.col = 0;
    vt_.row = 0;
    vt_.saved_col = 0;
    vt_.saved_row = 0;
    vt_.cursor_visible = true;
    vt_.pending_wrap = false;
    vt_.scroll_top = 0;
    vt_.scroll_bottom = std::max(0, grid_rows() - 1);
    vt_.auto_wrap_mode = true;
    vt_.origin_mode = false;
    vt_.cursor_app_mode = false;
    mouse_reporter_.reset();
    bracketed_paste_mode_ = false;
    alt_screen_.reset();
    scrollback_.reset();
    selection_.clear();
}

void TerminalHostBase::update_cursor_style()
{
    CursorStyle style = {};
    style.shape = CursorShape::Block;
    style.bg = highlights().default_fg();
    style.fg = highlights().default_bg();
    set_cursor_position(vt_.col, vt_.row);
    set_cursor_style(style, {}, !vt_.cursor_visible);
}

// ---------------------------------------------------------------------------
// Alternate screen
// ---------------------------------------------------------------------------

void TerminalHostBase::enter_alt_screen()
{
    alt_screen_.enter(vt_.col, vt_.row, vt_.scroll_top, vt_.scroll_bottom, vt_.pending_wrap);
    vt_.col = 0;
    vt_.row = 0;
    set_cursor_position(vt_.col, vt_.row);
}

void TerminalHostBase::leave_alt_screen()
{
    alt_screen_.leave(vt_.col, vt_.row, vt_.pending_wrap);
}

// ---------------------------------------------------------------------------
// Mouse reporting helpers
// ---------------------------------------------------------------------------

TerminalHostBase::GridPos TerminalHostBase::pixel_to_cell(int px, int py) const
{
    auto [cell_w, cell_h] = renderer().cell_size_pixels();
    const int pad = renderer().padding();
    if (cell_w <= 0)
        cell_w = 1;
    if (cell_h <= 0)
        cell_h = 1;
    const int col = std::clamp(
        (px - viewport().pixel_x - pad) / cell_w, 0, std::max(0, grid_cols() - 1));
    const int row = std::clamp(
        (py - viewport().pixel_y - pad) / cell_h, 0, std::max(0, grid_rows() - 1));
    return { col, row };
}

// ---------------------------------------------------------------------------
// Grid helpers
// ---------------------------------------------------------------------------

uint16_t TerminalHostBase::attr_id()
{
    if (!current_attr_.has_fg && !current_attr_.has_bg && !current_attr_.has_sp
        && !current_attr_.bold && !current_attr_.italic && !current_attr_.underline
        && !current_attr_.undercurl && !current_attr_.strikethrough && !current_attr_.reverse)
        return 0;

    // perf: O(1) amortized lookup via unordered_map (item 13 refactor).
    auto it = attr_cache_.find(current_attr_);
    if (it != attr_cache_.end())
        return it->second;

    const uint16_t id = next_attr_id_++;
    attr_cache_.try_emplace(current_attr_, id);
    highlights().set(id, current_attr_);
    return id;
}

void TerminalHostBase::clear_cell(int col, int row)
{
    grid().set_cell(col, row, " ", attr_id(), false);
}

void TerminalHostBase::newline(bool carriage_return)
{
    if (carriage_return)
        vt_.col = 0;
    vt_.pending_wrap = false;

    if (vt_.row == vt_.scroll_bottom)
    {
        // Save the top row of the scroll region to scrollback before scrolling.
        if (!alt_screen_.in_alt_screen() && vt_.scroll_top == 0 && vt_.scroll_bottom == grid_rows() - 1)
        {
            if (Cell* slot = scrollback_.next_write_slot())
            {
                const int cols = grid_cols();
                for (int col = 0; col < cols; ++col)
                    slot[col] = grid().get_cell(col, vt_.scroll_top);
                scrollback_.commit_push();
            }
        }
        grid().scroll(vt_.scroll_top, vt_.scroll_bottom + 1, 0, grid_cols(), 1);
    }
    else if (vt_.row < grid_rows() - 1)
    {
        ++vt_.row;
    }
    set_cursor_position(vt_.col, vt_.row);
}

void TerminalHostBase::write_cluster(const std::string& cluster)
{
    int width = cluster_cell_width(cluster);

    if (vt_.pending_wrap && vt_.auto_wrap_mode)
    {
        vt_.pending_wrap = false;
        newline(true);
    }

    vt_.col = std::clamp(vt_.col, 0, std::max(0, grid_cols() - 1));

    // Wide character at last available column: wrap first if auto-wrap enabled.
    if (width == 2 && vt_.col >= grid_cols() - 1)
    {
        if (vt_.auto_wrap_mode)
        {
            grid().set_cell(vt_.col, vt_.row, " ", attr_id(), false);
            newline(true);
        }
        else
        {
            width = 1;
        }
    }

    grid().set_cell(vt_.col, vt_.row, cluster, attr_id(), width == 2);
    const int new_col = vt_.col + width;

    if (new_col >= grid_cols())
    {
        vt_.pending_wrap = true;
        vt_.col = grid_cols() - 1;
    }
    else
    {
        vt_.col = new_col;
    }

    set_cursor_position(vt_.col, vt_.row);
}

void TerminalHostBase::erase_line(int mode)
{
    int start = 0;
    int end = grid_cols() - 1;
    if (mode == 0)
        start = vt_.col;
    else if (mode == 1)
        end = vt_.col;
    for (int col = start; col <= end; ++col)
        clear_cell(col, vt_.row);
}

void TerminalHostBase::erase_display(int mode)
{
    if (mode == 2)
    {
        grid().clear();
        return;
    }

    if (mode == 0)
    {
        erase_line(0);
        for (int row = vt_.row + 1; row < grid_rows(); ++row)
            for (int col = 0; col < grid_cols(); ++col)
                clear_cell(col, row);
    }
    else if (mode == 1)
    {
        erase_line(1);
        for (int row = 0; row < vt_.row; ++row)
            for (int col = 0; col < grid_cols(); ++col)
                clear_cell(col, row);
    }
}

} // namespace draxul
