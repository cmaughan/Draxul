#include <draxul/terminal_host_base.h>

#include <draxul/terminal_key_encoder.h>
#include <draxul/terminal_sgr.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <draxul/alt_screen_manager.h>
#include <draxul/base64.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/unicode.h>
#include <draxul/vt_parser.h>
#include <draxul/window.h>
#include <filesystem>
#include <fstream>
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
    PERF_MEASURE();
    ensure_pty_capture_ready();
    auto chunks = do_process_drain();
    const bool saw_output = !chunks.empty();
    if (!chunks.empty())
    {
        // Process all available output, re-draining after each batch so that
        // closely-spaced bursts are coalesced into a single flush. Bounded by
        // an 8 ms wall-clock budget so that runaway producers (e.g. `yes`,
        // `cat /dev/urandom`) cannot trap the main thread and starve the SDL
        // event loop — any remaining output is picked up on the next frame.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
        bool budget_exceeded = false;
        begin_output_cursor_batch();
        do
        {
            for (const auto& chunk : chunks)
            {
                maybe_capture_pty_chunk(chunk);
                consume_output(chunk);
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                budget_exceeded = true;
                break;
            }
            chunks = do_process_drain();
        } while (!chunks.empty());
        end_output_cursor_batch();

        if (budget_exceeded)
        {
            DRAXUL_LOG_DEBUG(LogCategory::App,
                "TerminalHostBase::pump drain budget (8 ms) exceeded; "
                "deferring remaining output to next frame");
        }

        if (!synchronized_output_active())
            flush_grid();
    }
    reconcile_provisional_cursor_after_pump(saw_output);
    trace_cursor_presentation_state("pump_end", saw_output);
    advance_cursor_blink(std::chrono::steady_clock::now());
}

std::optional<std::chrono::steady_clock::time_point> TerminalHostBase::next_deadline() const
{
    return GridHostBase::next_deadline();
}

void TerminalHostBase::on_focus_gained()
{
    GridHostBase::on_focus_gained();
    if (focus_reporting_mode_)
        do_process_write("\x1B[I");
}

void TerminalHostBase::on_focus_lost()
{
    GridHostBase::on_focus_lost();
    if (focus_reporting_mode_)
        do_process_write("\x1B[O");
}

void TerminalHostBase::on_key(const KeyEvent& event)
{
    PERF_MEASURE();
    if (!event.pressed)
        return;
    const std::string sequence = encode_terminal_key(event, vt_);
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        const std::string encoded = describe_text_for_log(sequence);
        log_printf(LogLevel::Trace, LogCategory::Input,
            "input trace: terminal_host_base on_key key=%d mod=0x%X encoded=%s",
            event.keycode,
            static_cast<unsigned int>(event.mod),
            encoded.c_str());
    }
    if (!sequence.empty())
        do_process_write(sequence);
}

void TerminalHostBase::on_text_input(const TextInputEvent& event)
{
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        const std::string described = describe_text_for_log(event.text);
        log_printf(LogLevel::Trace, LogCategory::Input,
            "input trace: terminal_host_base on_text_input text=%s len=%zu",
            described.c_str(),
            event.text.size());
    }
    if (!event.text.empty())
        do_process_write(event.text);
}

void TerminalHostBase::on_config_reloaded(const HostReloadConfig& config)
{
    GridHostBase::on_config_reloaded(config);

    launch_options().terminal_fg = config.terminal_fg;
    launch_options().terminal_bg = config.terminal_bg;
    launch_options().enable_osc8_hyperlinks = config.enable_osc8_hyperlinks;
    launch_options().enable_shell_integration_marks = config.enable_shell_integration_marks;

    highlights().set_default_fg(
        launch_options().terminal_fg.value_or(Color(0.92f, 0.92f, 0.92f, 1.0f)));
    highlights().set_default_bg(
        launch_options().terminal_bg.value_or(Color(0.08f, 0.09f, 0.10f, 1.0f)));
    force_full_redraw();
    flush_grid();
    update_cursor_style();
}

bool TerminalHostBase::dispatch_action(std::string_view action)
{
    PERF_MEASURE();
    if (action == "paste")
    {
        const std::string clip = window().clipboard_text();
        const int threshold = launch_options().paste_confirm_lines;
        if (threshold > 0 && !clip.empty())
        {
            const int newlines = static_cast<int>(std::count(clip.begin(), clip.end(), '\n'));
            // A clipboard payload of N newlines pastes N+1 logical lines.
            if (newlines + 1 >= threshold)
            {
                if (!pending_paste_.empty())
                {
                    callbacks().push_toast(
                        1, "Previous pending paste was replaced by a new paste.");
                }
                pending_paste_ = clip;
                char msg[160];
                std::snprintf(msg, sizeof(msg),
                    "Paste %d lines? Run confirm_paste to proceed, cancel_paste to discard.",
                    newlines + 1);
                callbacks().push_toast(1, msg);
                return true;
            }
        }
        send_paste(clip);
        return true;
    }
    if (action == "confirm_paste")
    {
        if (!pending_paste_.empty())
        {
            send_paste(pending_paste_);
            pending_paste_.clear();
        }
        return true;
    }
    if (action == "cancel_paste")
    {
        if (!pending_paste_.empty())
        {
            pending_paste_.clear();
            callbacks().push_toast(0, "Paste cancelled.");
        }
        return true;
    }
    return false;
}

void TerminalHostBase::send_paste(std::string_view text)
{
    PERF_MEASURE();
    if (bracketed_paste_mode_)
    {
        std::string wrapped;
        wrapped.reserve(text.size() + 12);
        wrapped += "\x1B[200~";
        wrapped += text;
        wrapped += "\x1B[201~";
        do_process_write(wrapped);
    }
    else
    {
        do_process_write(std::string(text));
    }
}

bool TerminalHostBase::ensure_pty_capture_ready()
{
    if (!pty_capture_config_loaded_)
    {
        pty_capture_config_loaded_ = true;
        if (!launch_options().pty_capture_file.empty())
            pty_capture_path_ = launch_options().pty_capture_file;
        else if (const char* value = std::getenv("DRAXUL_CAPTURE_PTY_FILE"))
            pty_capture_path_ = value;
    }

    if (pty_capture_path_.empty())
        return false;

    if (!pty_capture_header_checked_)
    {
        pty_capture_header_checked_ = true;

        const std::filesystem::path capture_path(pty_capture_path_);
        bool needs_header = true;
        std::ifstream existing(capture_path, std::ios::binary);
        if (existing)
        {
            std::string header_line;
            if (std::getline(existing, header_line) && header_line == "draxul-pty-capture-v1")
                needs_header = false;
        }

        if (needs_header)
        {
            std::ofstream out(capture_path, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                if (!pty_capture_failure_reported_)
                {
                    pty_capture_failure_reported_ = true;
                    DRAXUL_LOG_WARN(LogCategory::App,
                        "Failed to open PTY capture file '%s' for writing",
                        pty_capture_path_.c_str());
                }
                return false;
            }
            out << "draxul-pty-capture-v1\n";
        }

        if (!pty_capture_announced_)
        {
            pty_capture_announced_ = true;
            DRAXUL_LOG_INFO(LogCategory::App,
                "PTY capture enabled for host '%.*s': %s",
                static_cast<int>(host_name().size()),
                host_name().data(),
                pty_capture_path_.c_str());
        }
    }
    return true;
}

void TerminalHostBase::maybe_capture_pty_chunk(std::string_view bytes)
{
    if (!ensure_pty_capture_ready())
        return;

    std::ofstream out(pty_capture_path_, std::ios::binary | std::ios::app);
    if (!out)
    {
        if (!pty_capture_failure_reported_)
        {
            pty_capture_failure_reported_ = true;
            DRAXUL_LOG_WARN(LogCategory::App,
                "Failed to append PTY capture chunk to '%s'",
                pty_capture_path_.c_str());
        }
        return;
    }

    out << "chunk " << base64_encode(host_name()) << ' ' << base64_encode(bytes) << '\n';
}

// ---------------------------------------------------------------------------
// Viewport / font changes
// ---------------------------------------------------------------------------

void TerminalHostBase::on_viewport_changed()
{
    PERF_MEASURE();
    const int new_cols = std::max(1, viewport().grid_size.x);
    const int new_rows = std::max(1, viewport().grid_size.y);
    if (new_cols == grid_cols() && new_rows == grid_rows())
        return;

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "terminal: on_viewport_changed %dx%d -> %dx%d",
        grid_cols(), grid_rows(), new_cols, new_rows);

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

    const auto target_cursor = resize_preserved_cursor_.value_or(std::pair<int, int>{ vt_.col, vt_.row });
    if (target_cursor.first >= new_cols || target_cursor.second >= new_rows)
        resize_preserved_cursor_ = target_cursor;
    else
        resize_preserved_cursor_.reset();

    apply_grid_size(new_cols, new_rows);
    vt_.col = std::clamp(target_cursor.first, 0, std::max(0, grid_cols() - 1));
    vt_.row = std::clamp(target_cursor.second, 0, std::max(0, grid_rows() - 1));
    vt_.saved_col = std::clamp(vt_.saved_col, 0, std::max(0, grid_cols() - 1));
    vt_.saved_row = std::clamp(vt_.saved_row, 0, std::max(0, grid_rows() - 1));
    vt_.scroll_top = 0;
    vt_.scroll_bottom = grid_rows() - 1;
    set_cursor_position(vt_.col, vt_.row, CursorBlinkUpdate::Preserve);
    do_process_resize(new_cols, new_rows);
    force_full_redraw();
    flush_grid();
    update_cursor_style();
}

void TerminalHostBase::on_font_metrics_changed_impl()
{
    PERF_MEASURE();
    force_full_redraw();
    flush_grid();
}

// ---------------------------------------------------------------------------
// Terminal state reset
// ---------------------------------------------------------------------------

void TerminalHostBase::reset_terminal_state()
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
    current_hyperlink_id_ = 0;
    shell_marks_.clear();
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

void TerminalHostBase::update_cursor_style()
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
    CursorStyle style = {};
    style.shape = vt_.cursor_shape;
    style.bg = highlights().default_fg();
    style.fg = highlights().default_bg();

    // Apply blink timing when the shell requests a blinking cursor shape
    // (DECSCUSR odd Ps values). Standard terminal blink cadence: 530ms.
    BlinkTiming blink{};
    if (vt_.cursor_blink)
    {
        blink.blinkwait = 530;
        blink.blinkon = 530;
        blink.blinkoff = 530;
    }
    set_cursor_style(style, blink, !vt_.cursor_visible);
}

void TerminalHostBase::set_logical_cursor_position(int col, int row, CursorBlinkUpdate blink_update)
{
    vt_.col = std::clamp(col, 0, std::max(0, grid_cols() - 1));
    vt_.row = std::clamp(row, 0, std::max(0, grid_rows() - 1));
    set_cursor_position(vt_.col, vt_.row, blink_update);
}

void TerminalHostBase::trace_cursor_presentation_state(std::string_view stage, bool saw_output) const
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
        cursor_col(),
        cursor_row());
}

void TerminalHostBase::reconcile_provisional_cursor_after_pump(bool saw_output)
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

void TerminalHostBase::begin_synchronized_output()
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

void TerminalHostBase::end_synchronized_output()
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

void TerminalHostBase::begin_output_cursor_batch()
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

void TerminalHostBase::end_output_cursor_batch()
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
    set_cursor_position(vt_.col, vt_.row, CursorBlinkUpdate::Preserve);
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

void TerminalHostBase::enter_alt_screen()
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

void TerminalHostBase::leave_alt_screen()
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

uint16_t TerminalHostBase::attr_id()
{
    PERF_MEASURE();
    return attr_cache_.get_or_insert(
        current_attr_, highlights(), [this]() { compact_attr_ids(); });
}

void TerminalHostBase::compact_attr_ids()
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

void TerminalHostBase::clear_cell(int col, int row)
{
    grid().set_cell(col, row, " ", attr_id(), false);
}

void TerminalHostBase::newline(bool carriage_return)
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
        grid().scroll(vt_.scroll_top, vt_.scroll_bottom + 1, 0, grid_cols(), 1);
    }
    else if (vt_.row < grid_rows() - 1)
    {
        ++vt_.row;
    }
}

void TerminalHostBase::write_cluster(const std::string& cluster)
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

void TerminalHostBase::erase_line(int mode)
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

void TerminalHostBase::erase_display(int mode)
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

    callbacks().set_window_title(basename.empty() ? "/" : std::string(basename));
}

// ---------------------------------------------------------------------------
// Virtual hooks for highlight compaction (default no-ops)
// ---------------------------------------------------------------------------

void TerminalHostBase::collect_extra_attr_ids(std::unordered_map<uint16_t, HlAttr>& /*active_attrs*/)
{
    // Intentionally empty — LocalTerminalHost overrides to scan scrollback.
}

void TerminalHostBase::remap_extra_highlight_ids(const std::function<uint16_t(uint16_t)>& /*remap_fn*/)
{
    // Intentionally empty — LocalTerminalHost overrides to remap scrollback.
}

} // namespace draxul
