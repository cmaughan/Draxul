#include <draxul/terminal_core.h>

#include <draxul/terminal_sgr.h>

#include <algorithm>
#include <draxul/alt_screen_manager.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/unicode.h>
#include <draxul/vt_parser.h>
#include <functional>
#include <unordered_map>
#include <utility>

namespace draxul
{

namespace
{
void set_grid_cell_for_alt_screen(Grid& grid, int col, int row, const Cell& cell)
{
    grid.set_cell(col, row, std::string(cell.text.view()), cell.hl_attr_id, cell.double_width);
}

std::string dec_special_graphics(std::string_view cluster)
{
    if (cluster.size() != 1)
        return std::string(cluster);

    switch (cluster[0])
    {
    case '`':
        return "\xE2\x97\x86"; // ◆
    case 'a':
        return "\xE2\x96\x92"; // ▒
    case 'f':
        return "\xC2\xB0"; // °
    case 'g':
        return "\xC2\xB1"; // ±
    case 'h':
        return "\xE2\x90\xA4"; // ␤
    case 'i':
        return "\xE2\x90\x8B"; // ␋
    case 'j':
        return "\xE2\x94\x98"; // ┘
    case 'k':
        return "\xE2\x94\x90"; // ┐
    case 'l':
        return "\xE2\x94\x8C"; // ┌
    case 'm':
        return "\xE2\x94\x94"; // └
    case 'n':
        return "\xE2\x94\xBC"; // ┼
    case 'o':
        return "\xE2\x8E\xBA"; // ⎺
    case 'p':
        return "\xE2\x8E\xBB"; // ⎻
    case 'q':
        return "\xE2\x94\x80"; // ─
    case 'r':
        return "\xE2\x8E\xBC"; // ⎼
    case 's':
        return "\xE2\x8E\xBD"; // ⎽
    case 't':
        return "\xE2\x94\x9C"; // ├
    case 'u':
        return "\xE2\x94\xA4"; // ┤
    case 'v':
        return "\xE2\x94\xB4"; // ┴
    case 'w':
        return "\xE2\x94\xAC"; // ┬
    case 'x':
        return "\xE2\x94\x82"; // │
    case 'y':
        return "\xE2\x89\xA4"; // ≤
    case 'z':
        return "\xE2\x89\xA5"; // ≥
    case '{':
        return "\xCF\x80"; // π
    case '|':
        return "\xE2\x89\xA0"; // ≠
    case '}':
        return "\xC2\xA3"; // £
    case '~':
        return "\xC2\xB7"; // ·
    default:
        return std::string(cluster);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

TerminalCore::TerminalCore(ITerminalCoreHost& host)
    : host_(host)
    , vt_parser_(VtParser::Callbacks{
          std::bind_front(&TerminalCore::write_cluster, this),
          std::bind_front(&TerminalCore::handle_control, this),
          std::bind_front(&TerminalCore::handle_csi, this),
          std::bind_front(&TerminalCore::handle_osc, this),
          std::bind_front(&TerminalCore::handle_esc, this),
      })
    , alt_screen_(AltScreenManager::GridAccessors{
          std::bind_front(&TerminalCore::grid_cols, this),
          std::bind_front(&TerminalCore::grid_rows, this),
          std::bind_front(std::mem_fn(&Grid::get_cell), std::ref(grid())),
          std::bind_front(set_grid_cell_for_alt_screen, std::ref(grid())),
          std::bind_front(std::mem_fn(&Grid::clear), std::ref(grid())),
      })
{
}

// ---------------------------------------------------------------------------
// Terminal state and presentation
// ---------------------------------------------------------------------------
void TerminalCore::reset()
{
    PERF_MEASURE();
    current_attr_ = {};
    attr_cache_.clear();
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
    vt_.cursor_shape = CursorShape::Block;
    vt_.cursor_blink = false;
    bracketed_paste_mode_ = false;
    g0_charset_ = CharsetMode::Ascii;
    g1_charset_ = CharsetMode::Ascii;
    gl_uses_g1_charset_ = false;
    pending_charset_designation_ = '\0';
    focus_reporting_mode_ = false;
    mouse_modes_ = {};
    current_hyperlink_id_ = 0;
    shell_marks_.clear();
    terminal_title_.clear();
    current_cwd_.clear();
    output_cursor_batch_active_ = false;
    output_cursor_batch_saw_hide_ = false;
    output_cursor_batch_saw_show_ = false;
    output_cursor_batch_ended_synchronized_output_ = false;
    output_cursor_batch_start_col_ = 0;
    output_cursor_batch_start_row_ = 0;
    output_cursor_batch_start_visible_ = true;
    provisional_cursor_active_ = false;
    provisional_cursor_quiet_pumps_ = 0;
    stable_cursor_known_ = false;
    stable_cursor_col_ = 0;
    stable_cursor_row_ = 0;
    synchronized_output_mode_ = false;
    synchronized_output_saw_hide_ = false;
    synchronized_output_saw_show_ = false;
    set_cursor_display_override(std::nullopt);
    alt_screen_.reset();
}

void TerminalCore::resize(int cols, int rows)
{
    PERF_MEASURE();
    cols = std::max(1, cols);
    rows = std::max(1, rows);
    if (cols == grid_cols() && rows == grid_rows())
        return;

    std::erase_if(shell_marks_, [rows](const ShellMark& mark) {
        return mark.row < 0 || mark.row >= rows;
    });

    const int previous_cols = grid_cols();
    const int previous_rows = grid_rows();
    if (alt_screen_.in_alt_screen())
    {
        alt_screen_.resize_snapshot(cols, rows, previous_cols, previous_rows);
        alt_screen_.clamp_saved_cursor(
            std::max(0, cols - 1), std::max(0, rows - 1));
    }

    const auto target_cursor
        = resize_preserved_cursor_.value_or(std::pair<int, int>{ vt_.col, vt_.row });
    if (target_cursor.first >= cols || target_cursor.second >= rows)
        resize_preserved_cursor_ = target_cursor;
    else
        resize_preserved_cursor_.reset();

    host_.terminal_resize_grid(cols, rows);
    vt_.col = std::clamp(target_cursor.first, 0, std::max(0, grid_cols() - 1));
    vt_.row = std::clamp(target_cursor.second, 0, std::max(0, grid_rows() - 1));
    vt_.saved_col = std::clamp(vt_.saved_col, 0, std::max(0, grid_cols() - 1));
    vt_.saved_row = std::clamp(vt_.saved_row, 0, std::max(0, grid_rows() - 1));
    vt_.scroll_top = 0;
    vt_.scroll_bottom = grid_rows() - 1;
    set_cursor_position(
        vt_.col, vt_.row, TerminalCursorBlinkUpdate::Preserve);
    update_cursor_style();
}

void TerminalCore::update_cursor_style()
{
    PERF_MEASURE();
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: update_cursor_style vt_visible=%d shape=%d blink=%d logical=(%d,%d)",
            vt_.cursor_visible ? 1 : 0,
            static_cast<int>(vt_.cursor_shape),
            vt_.cursor_blink ? 1 : 0,
            vt_.col,
            vt_.row);
    }
    host_.terminal_set_cursor_style(
        vt_.cursor_shape, vt_.cursor_blink, vt_.cursor_visible);
}

void TerminalCore::set_logical_cursor_position(int col, int row, TerminalCursorBlinkUpdate blink_update)
{
    vt_.col = std::clamp(col, 0, std::max(0, grid_cols() - 1));
    vt_.row = std::clamp(row, 0, std::max(0, grid_rows() - 1));
    set_cursor_position(vt_.col, vt_.row, blink_update);
}

void TerminalCore::trace_cursor_presentation_state(std::string_view stage, bool saw_output) const
{
    PERF_MEASURE();
    if (!log_would_emit(LogLevel::Trace, LogCategory::Input))
        return;
    log_printf(LogLevel::Trace,
        LogCategory::Input,
        "cursor trace: %.*s saw_output=%d sync_output=%d alt_screen=%d vt=(%d,%d,v=%d) published=(%d,%d)",
        static_cast<int>(stage.size()),
        stage.data(),
        saw_output ? 1 : 0,
        synchronized_output_mode_ ? 1 : 0,
        alt_screen_.in_alt_screen() ? 1 : 0,
        vt_.col,
        vt_.row,
        vt_.cursor_visible ? 1 : 0,
        host_.terminal_published_cursor_position().first,
        host_.terminal_published_cursor_position().second);
}

void TerminalCore::reconcile_provisional_cursor_after_pump(bool saw_output)
{
    PERF_MEASURE();
    if (!provisional_cursor_active_)
        return;
    if (saw_output)
    {
        provisional_cursor_quiet_pumps_ = 0;
        return;
    }

    ++provisional_cursor_quiet_pumps_;
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: provisional_cursor quiet_pumps=%d logical=(%d,%d,v=%d) stable=(%d,%d)",
            provisional_cursor_quiet_pumps_,
            vt_.col,
            vt_.row,
            vt_.cursor_visible ? 1 : 0,
            stable_cursor_col_,
            stable_cursor_row_);
    }
    if (provisional_cursor_quiet_pumps_ < 2)
        return;

    provisional_cursor_active_ = false;
    provisional_cursor_quiet_pumps_ = 0;
    set_cursor_display_override(std::nullopt);
    if (vt_.cursor_visible)
    {
        stable_cursor_known_ = true;
        stable_cursor_col_ = vt_.col;
        stable_cursor_row_ = vt_.row;
    }
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: provisional_cursor released logical=(%d,%d,v=%d)",
            vt_.col,
            vt_.row,
            vt_.cursor_visible ? 1 : 0);
    }
}

void TerminalCore::begin_synchronized_output()
{
    PERF_MEASURE();
    if (synchronized_output_mode_)
        return;

    synchronized_output_mode_ = true;
    synchronized_output_saw_hide_ = false;
    synchronized_output_saw_show_ = false;
    begin_cursor_publish_batch();
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: begin_synchronized_output logical=(%d,%d) visible=%d",
            vt_.col,
            vt_.row,
            vt_.cursor_visible ? 1 : 0);
    }
}

void TerminalCore::end_synchronized_output()
{
    PERF_MEASURE();
    if (!synchronized_output_mode_)
        return;

    synchronized_output_mode_ = false;
    output_cursor_batch_ended_synchronized_output_ = true;
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: end_synchronized_output logical=(%d,%d) visible=%d hide=%d show=%d",
            vt_.col,
            vt_.row,
            vt_.cursor_visible ? 1 : 0,
            synchronized_output_saw_hide_ ? 1 : 0,
            synchronized_output_saw_show_ ? 1 : 0);
    }
    end_cursor_publish_batch();
}

void TerminalCore::begin_output_cursor_batch()
{
    PERF_MEASURE();
    if (output_cursor_batch_active_)
        return;
    output_cursor_batch_active_ = true;
    output_cursor_batch_saw_hide_ = false;
    output_cursor_batch_saw_show_ = false;
    output_cursor_batch_ended_synchronized_output_ = false;
    output_cursor_batch_start_col_ = vt_.col;
    output_cursor_batch_start_row_ = vt_.row;
    output_cursor_batch_start_visible_ = vt_.cursor_visible;
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: begin_output_cursor_batch start=(%d,%d,v=%d) sync_output=%d alt_screen=%d",
            output_cursor_batch_start_col_,
            output_cursor_batch_start_row_,
            output_cursor_batch_start_visible_ ? 1 : 0,
            synchronized_output_mode_ ? 1 : 0,
            alt_screen_.in_alt_screen() ? 1 : 0);
    }
    begin_cursor_publish_batch();
}

void TerminalCore::end_output_cursor_batch()
{
    PERF_MEASURE();
    if (!output_cursor_batch_active_)
        return;
    output_cursor_batch_active_ = false;
    const bool batch_had_cursor_churn = output_cursor_batch_saw_hide_ && output_cursor_batch_saw_show_;
    const bool sync_frame_had_cursor_churn = output_cursor_batch_ended_synchronized_output_
        && synchronized_output_saw_hide_
        && synchronized_output_saw_show_;
    const bool should_consider_provisional = !synchronized_output_mode_;
    const bool keep_previous_cursor_visible = should_consider_provisional
        && !alt_screen_.in_alt_screen()
        && vt_.cursor_visible
        && stable_cursor_known_
        && vt_.row != stable_cursor_row_;
    set_cursor_position(vt_.col, vt_.row, TerminalCursorBlinkUpdate::Preserve);
    if (keep_previous_cursor_visible && (batch_had_cursor_churn || sync_frame_had_cursor_churn))
    {
        provisional_cursor_active_ = true;
        provisional_cursor_quiet_pumps_ = 0;
        set_cursor_display_override(std::pair<int, int>{ stable_cursor_col_, stable_cursor_row_ });
    }
    else if (should_consider_provisional)
    {
        provisional_cursor_active_ = false;
        provisional_cursor_quiet_pumps_ = 0;
        set_cursor_display_override(std::nullopt);
        if (vt_.cursor_visible)
        {
            stable_cursor_known_ = true;
            stable_cursor_col_ = vt_.col;
            stable_cursor_row_ = vt_.row;
        }
    }
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: end_output_cursor_batch start=(%d,%d,v=%d) end=(%d,%d,v=%d) sync_output=%d ended_sync=%d alt_screen=%d hide=%d show=%d sync_hide=%d sync_show=%d provisional=%d stable=(%d,%d)",
            output_cursor_batch_start_col_,
            output_cursor_batch_start_row_,
            output_cursor_batch_start_visible_ ? 1 : 0,
            vt_.col,
            vt_.row,
            vt_.cursor_visible ? 1 : 0,
            synchronized_output_mode_ ? 1 : 0,
            output_cursor_batch_ended_synchronized_output_ ? 1 : 0,
            alt_screen_.in_alt_screen() ? 1 : 0,
            output_cursor_batch_saw_hide_ ? 1 : 0,
            output_cursor_batch_saw_show_ ? 1 : 0,
            synchronized_output_saw_hide_ ? 1 : 0,
            synchronized_output_saw_show_ ? 1 : 0,
            (keep_previous_cursor_visible && (batch_had_cursor_churn || sync_frame_had_cursor_churn)) ? 1 : 0,
            stable_cursor_col_,
            stable_cursor_row_);
    }
    end_cursor_publish_batch();
}

// ---------------------------------------------------------------------------
// Alternate screen
// ---------------------------------------------------------------------------

void TerminalCore::enter_alt_screen()
{
    PERF_MEASURE();
    if (synchronized_output_mode_)
        end_synchronized_output();
    provisional_cursor_active_ = false;
    provisional_cursor_quiet_pumps_ = 0;
    set_cursor_display_override(std::nullopt);
    alt_screen_.enter(vt_.col, vt_.row, vt_.scroll_top, vt_.scroll_bottom, vt_.pending_wrap);
    vt_.col = 0;
    vt_.row = 0;
}

void TerminalCore::leave_alt_screen()
{
    PERF_MEASURE();
    if (synchronized_output_mode_)
        end_synchronized_output();
    provisional_cursor_active_ = false;
    provisional_cursor_quiet_pumps_ = 0;
    set_cursor_display_override(std::nullopt);
    alt_screen_.leave(vt_.col, vt_.row, vt_.pending_wrap, vt_.scroll_top, vt_.scroll_bottom);
}

// ---------------------------------------------------------------------------
// Grid helpers
// ---------------------------------------------------------------------------

uint16_t TerminalCore::attr_id()
{
    PERF_MEASURE();
    return attr_cache_.get_or_insert(
        current_attr_, highlights(), [this]() { compact_attr_ids(); });
}

void TerminalCore::compact_attr_ids()
{
    PERF_MEASURE();

    // 1. Collect live attr IDs from all sources.
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

    alt_screen_.for_each_saved_cell(
        [&active_attrs, this](const Cell& cell) {
            if (cell.hl_attr_id == 0)
                return;
            active_attrs.try_emplace(cell.hl_attr_id, highlights().get(cell.hl_attr_id));
        });

    collect_extra_attr_ids(active_attrs);

    // 2. Compact via the shared AttributeCache.
    const auto remap = attr_cache_.compact(active_attrs, highlights());

    // 3. Apply the remap to all ID-bearing storage.
    auto remap_fn = [&remap](uint16_t id) -> uint16_t {
        if (id == 0)
            return id;
        const auto it = remap.find(id);
        return it != remap.end() ? it->second : static_cast<uint16_t>(0);
    };

    grid().remap_highlight_ids(remap_fn);
    alt_screen_.remap_saved_highlight_ids(remap_fn);
    remap_extra_highlight_ids(remap_fn);
}

void TerminalCore::clear_cell(int col, int row)
{
    grid().set_cell(col, row, " ", attr_id(), false);
}

void TerminalCore::scroll_rows(
    int top, int bottom, int rows)
{
    if (!alt_screen_.in_alt_screen() && rows != 0)
    {
        std::erase_if(shell_marks_,
            [top, bottom, rows](ShellMark& mark) {
                if (mark.row < top
                    || mark.row >= bottom)
                {
                    return false;
                }
                mark.row -= rows;
                return mark.row < top
                    || mark.row >= bottom;
            });
    }
    grid().scroll(
        top, bottom, 0, grid_cols(), rows);
}

void TerminalCore::newline(bool carriage_return)
{
    PERF_MEASURE();
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
        scroll_rows(
            vt_.scroll_top, vt_.scroll_bottom + 1, 1);
    }
    else if (vt_.row < grid_rows() - 1)
    {
        ++vt_.row;
    }
}

void TerminalCore::write_cluster(const std::string& cluster)
{
    PERF_MEASURE();
    if (pending_charset_designation_ != '\0')
    {
        const CharsetMode mode = (cluster == "0") ? CharsetMode::DecSpecialGraphics : CharsetMode::Ascii;
        if (pending_charset_designation_ == '(')
            g0_charset_ = mode;
        else if (pending_charset_designation_ == ')')
            g1_charset_ = mode;
        pending_charset_designation_ = '\0';
        return;
    }

    const CharsetMode active_charset = gl_uses_g1_charset_ ? g1_charset_ : g0_charset_;
    const std::string rendered_cluster = active_charset == CharsetMode::DecSpecialGraphics
        ? dec_special_graphics(cluster)
        : cluster;
    int width = cluster_cell_width(rendered_cluster);

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
            grid().set_cell_hyperlink_id(vt_.col, vt_.row, current_hyperlink_id_);
            newline(true);
        }
        else
        {
            width = 1;
        }
    }

    grid().set_cell(vt_.col, vt_.row, rendered_cluster, attr_id(), width == 2);
    grid().set_cell_hyperlink_id(vt_.col, vt_.row, current_hyperlink_id_);
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
}

void TerminalCore::erase_line(int mode)
{
    PERF_MEASURE();
    int start = 0;
    int end = grid_cols() - 1;
    if (mode == 0)
        start = vt_.col;
    else if (mode == 1)
        end = vt_.col;
    for (int col = start; col <= end; ++col)
        clear_cell(col, vt_.row);
}

void TerminalCore::erase_display(int mode)
{
    PERF_MEASURE();
    DRAXUL_LOG_DEBUG(LogCategory::App,
        "terminal: erase_display(%d) grid=%dx%d cursor=(%d,%d)",
        mode, grid_cols(), grid_rows(), vt_.col, vt_.row);
    if (mode == 2)
    {
        // Push non-blank visible rows to scrollback before clearing, so
        // 'clear' command output is preserved in history (matches iTerm2,
        // Windows Terminal). Only on the main screen.
        if (!alt_screen_.in_alt_screen())
        {
            for (int r = 0; r < grid_rows(); ++r)
            {
                bool blank_row = true;
                for (int c = 0; c < grid_cols(); ++c)
                {
                    const auto& cell = grid().get_cell(c, r);
                    if (!cell.text.empty() && cell.text.view() != " ")
                    {
                        blank_row = false;
                        break;
                    }
                }
                if (!blank_row)
                    on_line_scrolled_off(r);
            }
        }
        shell_marks_.clear();
        grid().clear();
        return;
    }

    if (mode == 0)
    {
        std::erase_if(shell_marks_,
            [this](const ShellMark& mark) {
                return mark.row > vt_.row
                    || (vt_.col == 0
                        && mark.row == vt_.row);
            });
        erase_line(0);
        for (int row = vt_.row + 1; row < grid_rows(); ++row)
            for (int col = 0; col < grid_cols(); ++col)
                clear_cell(col, row);
    }
    else if (mode == 1)
    {
        std::erase_if(shell_marks_,
            [this](const ShellMark& mark) {
                return mark.row < vt_.row
                    || (vt_.col
                            == grid_cols() - 1
                        && mark.row == vt_.row);
            });
        erase_line(1);
        for (int row = 0; row < vt_.row; ++row)
            for (int col = 0; col < grid_cols(); ++col)
                clear_cell(col, row);
    }
}

// ---------------------------------------------------------------------------
// OSC 7 — working directory change
// ---------------------------------------------------------------------------

void TerminalCore::on_osc_cwd(const std::string& path)
{
    PERF_MEASURE();
    // Cache the full path for the per-pane status bar (WI 78). The window
    // title (set below) only shows the basename for brevity.
    current_cwd_ = path;

    // Show the last path component (directory name) as the window title,
    // matching the convention used by most terminal emulators.
    std::string_view sv = path;

    // Strip trailing slash(es) so that "/tmp/" yields "tmp", not "".
    while (sv.size() > 1 && sv.back() == '/')
        sv.remove_suffix(1);

    const auto last_slash = sv.rfind('/');
    const std::string_view basename = (last_slash != std::string_view::npos) ? sv.substr(last_slash + 1) : sv;

    host_.terminal_set_title(basename.empty() ? "/" : basename);
}

TerminalSnapshotMetadata TerminalCore::snapshot_metadata() const
{
    TerminalSnapshotMetadata metadata;
    metadata.cursor = TerminalCursorSnapshot{
        .col = vt_.col,
        .row = vt_.row,
        .visible = vt_.cursor_visible,
        .shape = vt_.cursor_shape,
        .blink = vt_.cursor_blink,
    };
    metadata.modes = TerminalModeSnapshot{
        .alternate_screen = alt_screen_.in_alt_screen(),
        .auto_wrap = vt_.auto_wrap_mode,
        .origin = vt_.origin_mode,
        .cursor_application = vt_.cursor_app_mode,
        .bracketed_paste = bracketed_paste_mode_,
        .focus_reporting = focus_reporting_mode_,
        .synchronized_output = synchronized_output_mode_,
        .mouse = mouse_modes_,
    };
    metadata.title = terminal_title_;
    metadata.working_directory = current_cwd_;
    metadata.shell_marks.reserve(std::min(
        shell_marks_.size(), TerminalStateLimits::kMaxShellMarks));
    for (const ShellMark& source : shell_marks_)
    {
        if (metadata.shell_marks.size()
                >= TerminalStateLimits::kMaxShellMarks
            || source.row < 0 || source.row >= grid_rows())
        {
            continue;
        }
        TerminalShellMarkKind kind = TerminalShellMarkKind::PromptStart;
        switch (source.type)
        {
        case ShellMarkType::PromptStart:
            kind = TerminalShellMarkKind::PromptStart;
            break;
        case ShellMarkType::CommandStart:
            kind = TerminalShellMarkKind::CommandStart;
            break;
        case ShellMarkType::OutputStart:
            kind = TerminalShellMarkKind::OutputStart;
            break;
        case ShellMarkType::OutputEnd:
            kind = TerminalShellMarkKind::OutputEnd;
            break;
        }
        metadata.shell_marks.push_back(TerminalShellMarkSnapshot{
            .kind = kind,
            .row = source.row,
            .exit_code = source.exit_code,
        });
    }
    return metadata;
}

TerminalSemanticSnapshot TerminalCore::semantic_snapshot() const
{
    return capture_terminal_semantic_snapshot(
        grid(), highlights(), snapshot_metadata());
}

TerminalDirtySnapshot TerminalCore::dirty_snapshot() const
{
    return capture_terminal_dirty_snapshot(
        grid(), highlights(), snapshot_metadata());
}

} // namespace draxul
