#include <draxul/terminal_host_base.h>

#include <draxul/terminal_key_encoder.h>
#include <draxul/terminal_sgr.h>

#include <algorithm>
#include <draxul/alt_screen_manager.h>
#include <draxul/log.h>
#include <draxul/unicode.h>
#include <draxul/vt_parser.h>
#include <draxul/window.h>
#include <functional>
#include <unordered_map>

namespace draxul
{

namespace
{

void set_grid_cell_for_alt_screen(Grid& grid, int col, int row, const Cell& cell)
{
    grid.set_cell(col, row, std::string(cell.text.view()), cell.hl_attr_id, cell.double_width);
}

} // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

TerminalHostBase::TerminalHostBase()
    : vt_parser_(VtParser::Callbacks{
          std::bind_front(&TerminalHostBase::write_cluster, this),
          std::bind_front(&TerminalHostBase::handle_control, this),
          std::bind_front(&TerminalHostBase::handle_csi, this),
          std::bind_front(&TerminalHostBase::handle_osc, this),
          std::bind_front(&TerminalHostBase::handle_esc, this),
      })
    , alt_screen_(AltScreenManager::GridAccessors{
          std::bind_front(&TerminalHostBase::grid_cols, this),
          std::bind_front(&TerminalHostBase::grid_rows, this),
          std::bind_front(std::mem_fn(&Grid::get_cell), std::ref(grid())),
          std::bind_front(set_grid_cell_for_alt_screen, std::ref(grid())),
          std::bind_front(std::mem_fn(&Grid::clear), std::ref(grid())),
      })
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

    if (next_attr_id_ >= kAttrCompactionThreshold)
        compact_attr_ids();

    it = attr_cache_.find(current_attr_);
    if (it != attr_cache_.end())
        return it->second;

    const uint16_t id = next_attr_id_++;
    attr_cache_.try_emplace(current_attr_, id);
    highlights().set(id, current_attr_);
    return id;
}

void TerminalHostBase::compact_attr_ids()
{
    struct ActiveAttrCollector
    {
        TerminalHostBase* self;
        std::unordered_map<uint16_t, HlAttr>& active_attrs;

        void operator()(const Cell& cell) const
        {
            if (cell.hl_attr_id == 0)
                return;
            active_attrs.try_emplace(cell.hl_attr_id, self->highlights().get(cell.hl_attr_id));
        }
    };

    struct HighlightRemapper
    {
        const std::unordered_map<uint16_t, uint16_t>& remap;

        uint16_t operator()(uint16_t id) const
        {
            if (id == 0)
                return id;
            const auto it = remap.find(id);
            return it != remap.end() ? it->second : static_cast<uint16_t>(0);
        }
    };

    std::unordered_map<uint16_t, HlAttr> active_attrs;
    active_attrs.reserve(attr_cache_.size());

    for (int row = 0; row < grid_rows(); ++row)
    {
        for (int col = 0; col < grid_cols(); ++col)
        {
            const uint16_t id = grid().get_cell(col, row).hl_attr_id;
            if (id == 0)
                continue;
            active_attrs.try_emplace(id, highlights().get(id));
        }
    }

    alt_screen_.for_each_saved_cell(ActiveAttrCollector{ this, active_attrs });

    std::unordered_map<uint16_t, uint16_t> remap;
    remap.reserve(active_attrs.size());

    uint16_t next_id = 1;
    attr_cache_.clear();
    for (const auto& [old_id, attr] : active_attrs)
    {
        const uint16_t new_id = next_id;
        ++next_id;
        remap.try_emplace(old_id, new_id);
        attr_cache_.try_emplace(attr, new_id);
        highlights().set(new_id, attr);
    }

    grid().remap_highlight_ids(HighlightRemapper{ remap });
    alt_screen_.remap_saved_highlight_ids(HighlightRemapper{ remap });

    next_attr_id_ = next_id;
    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Compacted terminal highlight cache from %zu historical attrs to %zu active attrs",
        active_attrs.size(), attr_cache_.size());
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
