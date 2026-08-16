#pragma once

#include <draxul/terminal_core.h>
#include <draxul/terminal_surface_host_base.h>
#include <draxul/window.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace draxul
{

// UI/process adapter for the renderer-free TerminalCore. Concrete shell hosts
// provide the PTY/ConPTY operations; TerminalHostBase maps core presentation
// events onto the shared terminal surface (TerminalSurfaceHostBase →
// GridHostBase) and client desktop capabilities.
class TerminalHostBase : public TerminalSurfaceHostBase, private ITerminalCoreHost
{
public:
    TerminalHostBase();

    std::string init_error() const override
    {
        return init_error_;
    }
    bool is_running() const override
    {
        return do_process_is_running();
    }
    std::optional<int> exit_code() const override
    {
        return do_process_exit_code();
    }
    void shutdown() override
    {
        do_process_shutdown();
    }
    void request_close() override
    {
        do_process_request_close();
    }
    void pump() override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;
    void on_focus_gained() override;
    void on_focus_lost() override;
    void on_key(const KeyEvent& event) override;
    void on_text_input(const TextInputEvent& event) override;
    void on_config_reloaded(const HostReloadConfig& config) override;
    bool dispatch_action(std::string_view action) override;

    std::string current_working_directory() const override
    {
        return core_.current_working_directory();
    }

protected:
    bool initialize_host() override = 0;
    std::string_view host_name() const override = 0;
    void on_viewport_changed() override;
    void on_font_metrics_changed_impl() override;

    virtual bool do_process_write(std::string_view text) = 0;
    virtual std::vector<std::string> do_process_drain() = 0;
    virtual bool do_process_resize(int cols, int rows) = 0;
    virtual bool do_process_is_running() const = 0;
    virtual std::optional<int> do_process_exit_code() const
    {
        return std::nullopt;
    }
    virtual void do_process_shutdown() = 0;
    virtual void do_process_request_close()
    {
        do_process_shutdown();
    }

    virtual void reset_terminal_state()
    {
        core_.set_config(TerminalCoreConfig{
            launch_options().enable_osc8_hyperlinks,
            launch_options().enable_shell_integration_marks,
        });
        core_.reset();
    }
    void update_cursor_style()
    {
        core_.update_cursor_style();
    }
    void consume_output(std::string_view bytes)
    {
        core_.feed(bytes);
    }
    void set_init_error(std::string error)
    {
        init_error_ = std::move(error);
    }
    const VtState& vt_state() const
    {
        return core_.vt_state();
    }
    const std::string& terminal_title() const
    {
        return core_.terminal_title();
    }
    void set_logical_cursor_position(int col, int row,
        CursorBlinkUpdate blink_update = CursorBlinkUpdate::Preserve)
    {
        core_.set_logical_cursor_position(col, row,
            blink_update == CursorBlinkUpdate::Restart
                ? TerminalCursorBlinkUpdate::Restart
                : TerminalCursorBlinkUpdate::Preserve);
    }
    size_t attr_cache_size() const
    {
        return core_.attr_cache_size();
    }
    const AttributeCache& attr_cache() const
    {
        return core_.attr_cache();
    }
    TerminalSemanticSnapshot semantic_snapshot() const
    {
        return core_.semantic_snapshot();
    }
    void begin_output_cursor_batch()
    {
        core_.begin_output_cursor_batch();
    }
    void end_output_cursor_batch()
    {
        core_.end_output_cursor_batch();
    }
    bool ensure_pty_capture_ready();
    void maybe_capture_pty_chunk(std::string_view bytes);
    void trace_cursor_presentation_state(std::string_view stage, bool saw_output) const
    {
        core_.trace_cursor_presentation_state(stage, saw_output);
    }
    void reconcile_provisional_cursor_after_pump(bool saw_output)
    {
        core_.reconcile_provisional_cursor_after_pump(saw_output);
    }
    bool synchronized_output_active() const
    {
        return core_.synchronized_output_active();
    }

    // Default surface seams for local PTY-backed hosts: mouse reports go to
    // the process, the copy-mode cursor starts at the VT cursor, and hosts
    // without a scrollback view ignore surface scrolling.
    void surface_write_mouse_report(std::string_view sequence) override
    {
        do_process_write(sequence);
    }
    void surface_scroll_lines(int /*rows*/) override {}
    glm::ivec2 surface_cursor_position() const override
    {
        return { vt_state().col, vt_state().row };
    }

    virtual void on_line_scrolled_off(int /*row*/) {}
    virtual void on_mouse_mode_changed(int /*mode*/, bool /*enable*/) {}
    virtual void collect_extra_attr_ids(
        std::unordered_map<uint16_t, HlAttr>& /*active_attrs*/)
    {
    }
    virtual void remap_extra_highlight_ids(
        const std::function<uint16_t(uint16_t)>& /*remap_fn*/)
    {
    }

private:
    void send_paste(std::string_view text);

    Grid& terminal_grid() override
    {
        return grid();
    }
    const Grid& terminal_grid() const override
    {
        return grid();
    }
    HighlightTable& terminal_highlights() override
    {
        return highlights();
    }
    const HighlightTable& terminal_highlights() const override
    {
        return highlights();
    }
    void terminal_resize_grid(int cols, int rows) override
    {
        apply_grid_size(cols, rows);
    }
    bool terminal_write_process(std::string_view bytes) override
    {
        return do_process_write(bytes);
    }
    void terminal_mark_activity() override
    {
        mark_activity();
    }
    void terminal_set_title(std::string_view title) override
    {
        callbacks().set_window_title(std::string(title));
    }
    std::string terminal_read_clipboard() const override
    {
        return window().clipboard_text();
    }
    void terminal_write_clipboard(std::string_view text) override
    {
        window().set_clipboard_text(std::string(text));
    }
    void terminal_set_cursor_position(
        int col, int row, TerminalCursorBlinkUpdate blink_update) override
    {
        set_cursor_position(col, row,
            blink_update == TerminalCursorBlinkUpdate::Restart
                ? CursorBlinkUpdate::Restart
                : CursorBlinkUpdate::Preserve);
    }
    std::pair<int, int> terminal_published_cursor_position() const override
    {
        return { cursor_col(), cursor_row() };
    }
    void terminal_set_cursor_display_override(
        std::optional<std::pair<int, int>> position) override
    {
        set_cursor_display_override(position);
    }
    void terminal_set_cursor_style(
        CursorShape shape, bool blink, bool visible) override;
    void terminal_begin_cursor_publish_batch() override
    {
        begin_cursor_publish_batch();
    }
    void terminal_end_cursor_publish_batch() override
    {
        end_cursor_publish_batch();
    }
    void terminal_line_scrolled_off(int row) override
    {
        on_line_scrolled_off(row);
    }
    void terminal_mouse_mode_changed(int mode, bool enable) override
    {
        on_mouse_mode_changed(mode, enable);
    }
    void terminal_collect_extra_attr_ids(
        std::unordered_map<uint16_t, HlAttr>& active_attrs) override
    {
        collect_extra_attr_ids(active_attrs);
    }
    void terminal_remap_extra_highlight_ids(
        const std::function<uint16_t(uint16_t)>& remap_fn) override
    {
        remap_extra_highlight_ids(remap_fn);
    }

    TerminalCore core_;
    bool pty_capture_config_loaded_ = false;
    bool pty_capture_header_checked_ = false;
    bool pty_capture_failure_reported_ = false;
    bool pty_capture_announced_ = false;
    std::string pty_capture_path_;
    std::string pending_paste_;
    std::string init_error_;
};

} // namespace draxul
