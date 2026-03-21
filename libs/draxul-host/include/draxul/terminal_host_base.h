#pragma once

#include <draxul/alt_screen_manager.h>
#include <draxul/grid_host_base.h>
#include <draxul/vt_parser.h>
#include <draxul/vt_state.h>
#include <string>
#include <string_view>
#include <unordered_map>
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
    void shutdown() override
    {
        do_process_shutdown();
    }
    void request_close() override
    {
        do_process_write("exit\r");
    }
    void pump() override;
    void on_key(const KeyEvent& event) override;
    void on_text_input(const TextInputEvent& event) override;
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
    virtual void do_process_shutdown() = 0;

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

private:
    uint16_t attr_id();
    void clear_cell(int col, int row);
    void newline(bool carriage_return);
    void write_cluster(const std::string& cluster);
    void erase_line(int mode);
    void erase_display(int mode);
    void handle_control(char ch);
    void handle_esc(char ch);
    void handle_csi(char final_char, std::string_view body);
    void handle_osc(std::string_view body) const;

    // CSI dispatch helpers — each handles one logical group of sequences.
    // Called from handle_csi(); pure behavioral extraction, no logic changes.
    void csi_cursor_move(char final_char, const std::vector<int>& params);
    void csi_erase(char final_char, const std::vector<int>& params);
    void csi_scroll(char final_char, bool private_mode, const std::vector<int>& params);
    void csi_insert_delete(char final_char, const std::vector<int>& params);
    void csi_sgr(const std::vector<int>& params);
    void csi_mode(char final_char, bool private_mode, const std::vector<int>& params);
    void csi_margins(bool private_mode, const std::vector<int>& params);

    void enter_alt_screen();
    void leave_alt_screen();

    // SGR / highlight state
    HlAttr current_attr_ = {};
    // perf: O(1) via unordered_map (replaced linear std::vector scan)
    std::unordered_map<HlAttr, uint16_t, HlAttrHash> attr_cache_;
    uint16_t next_attr_id_ = 1;

    // VT parser
    VtParser vt_parser_;

    // Cursor position, scroll region, and mode flags
    VtState vt_;

    // Alternate screen
    AltScreenManager alt_screen_;

    // Bracketed paste
    bool bracketed_paste_mode_ = false;

    std::string init_error_;
};

} // namespace draxul
