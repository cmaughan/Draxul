#pragma once

#include <draxul/mouse_reporter.h>
#include <draxul/scrollback_buffer.h>
#include <draxul/selection_manager.h>
#include <draxul/terminal_host_base.h>

#include <optional>

namespace draxul
{

// Intermediate base for local-process terminal hosts (shell, PowerShell, WSL).
// Extends TerminalHostBase with scrollback, selection, and mouse reporting —
// features that only make sense for hosts backed by a local PTY or ConPTY.
// NvimHost derives from GridHostBase directly and does not use this class.
class LocalTerminalHost : public TerminalHostBase
{
public:
    LocalTerminalHost();

    bool initialize(const HostContext& context, IHostCallbacks& callbacks) override;
    void on_config_reloaded(const HostReloadConfig& config) override;
    void pump() override;
    void on_key(const KeyEvent& event) override;
    void on_text_input(const TextInputEvent& event) override;
    void on_mouse_button(const MouseButtonEvent& event) override;
    void on_mouse_move(const MouseMoveEvent& event) override;
    void on_mouse_wheel(const MouseWheelEvent& event) override;
    std::optional<MouseCursor> mouse_cursor_at(int px, int py) const override;
    void set_scroll_offset(float px) override;
    bool dispatch_action(std::string_view action) override;
    std::string status_text() const override;
    std::optional<AgentObservation> capture_agent_observation(
        int max_rows, size_t max_bytes) const override;
    bool send_agent_input(std::string_view bytes) override;

protected:
    void on_viewport_changed() override;
    void reset_terminal_state() override;

private:
    struct GridPos
    {
        int col = 0;
        int row = 0;
    };

    GridPos pixel_to_cell(int px, int py) const;
    bool open_link_at(const GridPos& pos, ModifierFlags mod);
    void on_line_scrolled_off(int row) override;
    void on_mouse_mode_changed(int mode, bool enable) override;
    void collect_extra_attr_ids(std::unordered_map<uint16_t, HlAttr>& active_attrs) override;
    void remap_extra_highlight_ids(const std::function<uint16_t(uint16_t)>& remap_fn) override;

protected:
    SelectionManager& selection()
    {
        return selection_;
    }

private:
    // Vim/tmux-style keyboard copy mode. When active, key events navigate a
    // separate copy-mode cursor instead of being forwarded to the underlying
    // terminal process. The cursor is rendered as a single-cell selection
    // overlay so the user can see where they are.
    struct CopyMode
    {
        bool active = false;
        bool selecting = false;
        bool line_mode = false;
        glm::ivec2 cursor{ 0, 0 };
        glm::ivec2 anchor{ 0, 0 };
    };

    void enter_copy_mode();
    void exit_copy_mode(bool yank);
    bool handle_copy_mode_key(const KeyEvent& event);
    bool copy_active_selection_to_clipboard();
    void update_copy_mode_overlay();

    MouseReporter mouse_reporter_;
    SelectionManager selection_;
    ScrollbackBuffer scrollback_;
    CopyMode copy_mode_;
    std::optional<GridPos> pending_selection_copy_click_;
    bool suppress_next_selection_copy_text_input_ = false;

    // Snapshot of the grid taken before SIGWINCH. After the shell redraws,
    // pump() restores rows that the shell left blank but had content before.
    // This mimics tmux's virtual screen buffer: the shell's erase-and-redraw
    // doesn't destroy content the shell didn't explicitly overwrite.
    struct ResizeSnapshot
    {
        std::vector<Cell> cells;
        int cols = 0;
        int rows = 0;
        bool active = false;
    };
    ResizeSnapshot resize_snapshot_;
    uint64_t agent_output_generation_ = 0;
    std::optional<std::chrono::steady_clock::time_point> agent_last_output_at_;
};

} // namespace draxul
