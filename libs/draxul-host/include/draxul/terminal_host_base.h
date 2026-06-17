#pragma once

#include <draxul/alt_screen_manager.h>
#include <draxul/attribute_cache.h>
#include <draxul/grid_host_base.h>
#include <draxul/vt_parser.h>
#include <draxul/vt_state.h>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace draxul
{

// Abstract base for terminal-emulator hosts (PowerShell, bash, zsh, etc.).
// Provides a complete VT100/xterm state machine, grid-writing helpers, and
// standard IHost implementations. Concrete subclasses implement the five
// do_process_* pure virtuals to wire up a process back-end, plus
// initialize_host() and host_name().
//
// Scrollback, selection, and mouse reporting live in LocalTerminalHost, which
// is the intermediate base for all local-process hosts.
class TerminalHostBase : public GridHostBase
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

protected:
    bool initialize_host() override = 0;
    std::string_view host_name() const override = 0;
    void on_viewport_changed() override;
    void on_font_metrics_changed_impl() override;

    // Process back-end — subclass implements these five methods.
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

    // Terminal helpers available to subclasses.
    virtual void reset_terminal_state();
    void update_cursor_style();
    void consume_output(std::string_view bytes);
    void set_init_error(std::string error)
    {
        init_error_ = std::move(error);
    }
    const VtState& vt_state() const
    {
        return vt_;
    }
    void set_logical_cursor_position(
        int col,
        int row,
        CursorBlinkUpdate blink_update = CursorBlinkUpdate::Preserve);
    size_t attr_cache_size() const
    {
        return attr_cache_.size();
    }
    const AttributeCache& attr_cache() const
    {
        return attr_cache_;
    }
    void begin_output_cursor_batch();
    void end_output_cursor_batch();
    bool ensure_pty_capture_ready();
    void maybe_capture_pty_chunk(std::string_view bytes);
    void trace_cursor_presentation_state(std::string_view stage, bool saw_output) const;
    void reconcile_provisional_cursor_after_pump(bool saw_output);
    bool synchronized_output_active() const
    {
        return synchronized_output_mode_;
    }

    // Hook called by newline() when a line scrolls off the top of the visible
    // area (full-screen scroll region, main screen only). Override in
    // LocalTerminalHost to capture the row into the scrollback buffer.
    virtual void on_line_scrolled_off(int /*row*/)
    {
        // Intentionally empty — LocalTerminalHost overrides to capture rows into scrollback.
    }

    // Hook called by csi_mode() when the terminal process requests a mouse
    // reporting mode change. Override in LocalTerminalHost to forward to the
    // MouseReporter.
    virtual void on_mouse_mode_changed(int /*mode*/, bool /*enable*/)
    {
        // Intentionally empty — LocalTerminalHost overrides to forward to MouseReporter.
    }

    // Hooks called by compact_attr_ids() to let subclasses participate in
    // highlight compaction.  LocalTerminalHost overrides these to include
    // scrollback buffer cells.
    virtual void collect_extra_attr_ids(std::unordered_map<uint16_t, HlAttr>& active_attrs);
    virtual void remap_extra_highlight_ids(const std::function<uint16_t(uint16_t)>& remap_fn);

    // Hook called by handle_osc() when the shell emits an OSC 7
    // directory-change notification (file://hostname/path).  The decoded
    // filesystem path is passed directly — no URL prefix, no percent-encoding.
    // Default implementation stores the path in current_cwd_ and updates the
    // window title to the directory basename.
    virtual void on_osc_cwd(const std::string& path);

    // Most recent OSC 7 working directory reported by the shell. Empty until
    // the first OSC 7 sequence is observed. Stored in protected scope so
    // subclasses (e.g. LocalTerminalHost::status_text) can read it directly.
    std::string current_cwd_;

public:
    std::string current_working_directory() const override
    {
        return current_cwd_;
    }

protected:
private:
    enum class CharsetMode
    {
        Ascii,
        DecSpecialGraphics,
    };

    uint16_t attr_id();
    void compact_attr_ids();
    void clear_cell(int col, int row);
    void newline(bool carriage_return);
    void write_cluster(const std::string& cluster);
    void erase_line(int mode);
    void erase_display(int mode);
    void handle_control(char ch);
    void handle_esc(char ch);
    void handle_csi(char final_char, std::string_view body);
    void handle_osc(std::string_view body);
    void handle_osc8(std::string_view payload);
    void handle_osc133(std::string_view payload);

    // CSI dispatch helpers — each handles one logical group of sequences.
    // Called from handle_csi(); pure behavioral extraction, no logic changes.
    void csi_cursor_move(char final_char, const std::vector<int>& params);
    void csi_erase(char final_char, const std::vector<int>& params);
    void csi_scroll(char final_char, bool private_mode, const std::vector<int>& params);
    void csi_insert_delete(char final_char, const std::vector<int>& params);
    void csi_sgr(const std::vector<int>& params);
    void csi_mode(char final_char, bool private_mode, const std::vector<int>& params);
    void csi_dsr(bool private_mode, const std::vector<int>& params);
    void csi_da(bool private_mode, const std::vector<int>& params);
    void csi_margins(bool private_mode, const std::vector<int>& params);
    void begin_synchronized_output();
    void end_synchronized_output();

    void enter_alt_screen();
    void leave_alt_screen();

    enum class ShellMarkType
    {
        PromptStart,
        CommandStart,
        OutputStart,
        OutputEnd,
    };

    struct ShellMark
    {
        ShellMarkType type = ShellMarkType::PromptStart;
        int row = 0;
        int exit_code = -1;
    };

    // SGR / highlight state
    HlAttr current_attr_ = {};
    // Shared attribute-to-ID cache (replaces inline map + counter).
    AttributeCache attr_cache_;

    // VT parser
    VtParser vt_parser_;

    // Cursor position, scroll region, and mode flags
    VtState vt_;

    // Alternate screen
    AltScreenManager alt_screen_;

    // Bracketed paste
    bool bracketed_paste_mode_ = false;

    bool output_cursor_batch_active_ = false;
    bool output_cursor_batch_saw_hide_ = false;
    bool output_cursor_batch_saw_show_ = false;
    bool output_cursor_batch_ended_synchronized_output_ = false;
    int output_cursor_batch_start_col_ = 0;
    int output_cursor_batch_start_row_ = 0;
    bool output_cursor_batch_start_visible_ = true;
    bool provisional_cursor_active_ = false;
    int provisional_cursor_quiet_pumps_ = 0;
    bool stable_cursor_known_ = false;
    int stable_cursor_col_ = 0;
    int stable_cursor_row_ = 0;
    std::optional<std::pair<int, int>> resize_preserved_cursor_;
    bool synchronized_output_mode_ = false;
    bool synchronized_output_saw_hide_ = false;
    bool synchronized_output_saw_show_ = false;
    CharsetMode g0_charset_ = CharsetMode::Ascii;
    CharsetMode g1_charset_ = CharsetMode::Ascii;
    bool gl_uses_g1_charset_ = false;
    char pending_charset_designation_ = '\0';
    bool focus_reporting_mode_ = false;
    uint16_t current_hyperlink_id_ = 0;
    std::vector<ShellMark> shell_marks_;
    bool pty_capture_config_loaded_ = false;
    bool pty_capture_header_checked_ = false;
    bool pty_capture_failure_reported_ = false;
    bool pty_capture_announced_ = false;
    std::string pty_capture_path_;

    // Pending paste awaiting user confirmation. When non-empty, the next
    // confirm_paste action sends it; cancel_paste discards it. Populated by
    // dispatch_action("paste") when the clipboard payload contains at least
    // launch_options().paste_confirm_lines newlines.
    std::string pending_paste_;

    // Send `text` to the host process, wrapped in bracketed-paste markers when
    // the terminal has enabled bracketed paste mode.
    void send_paste(std::string_view text);

    std::string init_error_;
};

} // namespace draxul
