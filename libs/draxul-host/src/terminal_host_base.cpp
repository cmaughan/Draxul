#include <draxul/terminal_host_base.h>

#include <draxul/base64.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/string_util.h>
#include <draxul/terminal_key_encoder.h>
#include <draxul/window.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace draxul
{

TerminalHostBase::TerminalHostBase()
    : core_(*this)
{
}

void TerminalHostBase::pump()
{
    PERF_MEASURE();
    ensure_pty_capture_ready();
    auto chunks = do_process_drain();
    const bool saw_output = !chunks.empty();
    if (!chunks.empty())
    {
        const auto deadline
            = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
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
    if (core_.focus_reporting_mode())
        do_process_write("\x1B[I");
}

void TerminalHostBase::on_focus_lost()
{
    TerminalSurfaceHostBase::on_focus_lost();
    if (core_.focus_reporting_mode())
        do_process_write("\x1B[O");
}

void TerminalHostBase::on_key(const KeyEvent& event)
{
    PERF_MEASURE();
    if (!event.pressed)
        return;
    const std::string sequence = encode_terminal_key(event, core_.vt_state());
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
    TerminalSurfaceHostBase::on_config_reloaded(config);

    launch_options().terminal_fg = config.terminal_fg;
    launch_options().terminal_bg = config.terminal_bg;
    launch_options().enable_osc8_hyperlinks = config.enable_osc8_hyperlinks;
    launch_options().enable_shell_integration_marks = config.enable_shell_integration_marks;
    core_.set_config(TerminalCoreConfig{
        config.enable_osc8_hyperlinks,
        config.enable_shell_integration_marks,
    });

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
            const int newlines
                = static_cast<int>(std::count(clip.begin(), clip.end(), '\n'));
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
    if (core_.bracketed_paste_mode())
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
            if (std::getline(existing, header_line)
                && header_line == "draxul-pty-capture-v1")
            {
                needs_header = false;
            }
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

    out << "chunk " << base64_encode(host_name()) << ' '
        << base64_encode(bytes) << '\n';
}

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

    core_.resize(new_cols, new_rows);
    do_process_resize(new_cols, new_rows);
    force_full_redraw();
    flush_grid();
}

void TerminalHostBase::on_font_metrics_changed_impl()
{
    PERF_MEASURE();
    force_full_redraw();
    flush_grid();
}

void TerminalHostBase::terminal_set_cursor_style(
    CursorShape shape, bool blink, bool visible)
{
    CursorStyle style{};
    style.shape = shape;
    style.bg = highlights().default_fg();
    style.fg = highlights().default_bg();

    BlinkTiming timing{};
    if (blink)
    {
        timing.blinkwait = 530;
        timing.blinkon = 530;
        timing.blinkoff = 530;
    }
    set_cursor_style(style, timing, !visible);
}

} // namespace draxul
