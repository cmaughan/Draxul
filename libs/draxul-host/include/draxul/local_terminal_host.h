#pragma once

#include <draxul/scrollback_buffer.h>
#include <draxul/terminal_host_base.h>

#include <optional>

namespace draxul
{

// Intermediate base for local-process terminal hosts (shell, PowerShell, WSL).
// Extends TerminalHostBase with scrollback — the selection, copy-mode, and
// mouse surface lives in TerminalSurfaceHostBase, shared with the remote
// terminal host. NvimHost derives from GridHostBase directly and does not use
// this class.
class LocalTerminalHost : public TerminalHostBase
{
public:
    LocalTerminalHost();

    bool initialize(const HostContext& context, IHostCallbacks& callbacks) override;
    void on_config_reloaded(const HostReloadConfig& config) override;
    void pump() override;
    void on_key(const KeyEvent& event) override;
    void on_text_input(const TextInputEvent& event) override;
    bool dispatch_action(std::string_view action) override;
    std::string status_text() const override;
    std::optional<AgentObservation> capture_agent_observation(
        int max_rows, size_t max_bytes) const override;
    bool send_agent_input(std::string_view bytes) override;

protected:
    void on_viewport_changed() override;
    void reset_terminal_state() override;
    void surface_scroll_lines(int rows) override;

private:
    void on_line_scrolled_off(int row) override;
    void on_mouse_mode_changed(int mode, bool enable) override;
    void collect_extra_attr_ids(std::unordered_map<uint16_t, HlAttr>& active_attrs) override;
    void remap_extra_highlight_ids(const std::function<uint16_t(uint16_t)>& remap_fn) override;

    ScrollbackBuffer scrollback_;

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
