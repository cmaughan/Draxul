#include <draxul/terminal_host_base.h>

#include <draxul/terminal_key_encoder.h>
#include <draxul/terminal_sgr.h>

#include <algorithm>
#include <draxul/alt_screen_manager.h>
#include <draxul/log.h>
#include <draxul/unicode.h>
#include <draxul/vt_parser.h>
#include <draxul/window.h>

namespace draxul
{

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
        // Process all available output, re-draining after each batch so that
        // closely-spaced bursts are coalesced into a single flush.
        do
        {
            for (const auto& chunk : chunks)
                consume_output(chunk);
            chunks = do_process_drain();
        } while (!chunks.empty());

        flush_grid();
    }
    advance_cursor_blink(std::chrono::steady_clock::now());
}

void TerminalHostBase::on_key(const KeyEvent& event)
{
    if (!event.pressed)
        return;
    const std::string sequence = encode_terminal_key(event, vt_);
    if (!sequence.empty())
        do_process_write(sequence);
}

void TerminalHostBase::on_text_input(const TextInputEvent& event)
{
    if (!event.text.empty())
        do_process_write(event.text);
}

bool TerminalHostBase::dispatch_action(std::string_view action)
{
    if (action == "paste")
    {
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
    return false;
}

// ---------------------------------------------------------------------------
// Viewport / font changes
// ---------------------------------------------------------------------------

void TerminalHostBase::on_viewport_changed()
{
    const int new_cols = std::max(1, viewport().grid_size.x);
    const int new_rows = std::max(1, viewport().grid_size.y);
    if (new_cols == grid_cols() && new_rows == grid_rows())
        return;

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
    flush_grid();
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
    bracketed_paste_mode_ = false;
    alt_screen_.reset();
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
    alt_screen_.leave(vt_.col, vt_.row, vt_.pending_wrap, vt_.scroll_top, vt_.scroll_bottom);
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
        // Notify subclasses that a line is about to scroll off the top of the
        // visible area so they can capture it (e.g. into a scrollback buffer).
        if (!alt_screen_.in_alt_screen() && vt_.scroll_top == 0
            && vt_.scroll_bottom == grid_rows() - 1)
        {
            on_line_scrolled_off(vt_.scroll_top);
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

// ---------------------------------------------------------------------------
// OSC 7 — working directory change
// ---------------------------------------------------------------------------

void TerminalHostBase::on_osc_cwd(const std::string& path)
{
    if (!callbacks().set_window_title)
        return;

    // Show the last path component (directory name) as the window title,
    // matching the convention used by most terminal emulators.
    std::string_view sv = path;

    // Strip trailing slash(es) so that "/tmp/" yields "tmp", not "".
    while (sv.size() > 1 && sv.back() == '/')
        sv.remove_suffix(1);

    const auto last_slash = sv.rfind('/');
    const std::string_view basename = (last_slash != std::string_view::npos) ? sv.substr(last_slash + 1) : sv;

    callbacks().set_window_title(basename.empty() ? "/" : std::string(basename));
}

} // namespace draxul
