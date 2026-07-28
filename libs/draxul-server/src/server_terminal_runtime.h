#pragma once

#include "remote_terminal_runtime.h"

#include <draxul/terminal_core.h>

#ifdef _WIN32
#include <draxul/conpty_process.h>
#else
#include <draxul/unix_pty_process.h>
#endif

#include <optional>
#include <string>
#include <unordered_map>

namespace draxul
{

class ServerTerminalRuntime final
    : public IRemoteTerminalRuntime
    , private ITerminalCoreHost
{
public:
    ServerTerminalRuntime();
    ~ServerTerminalRuntime() override;

    bool ensure_started(std::string& error) override;
    bool restart(std::string& error) override;
    bool pump() override;
    bool send_input(std::string_view bytes) override;
    bool resize(int cols, int rows) override;
    bool is_running() const override;
    uint64_t process_id() const override;
    TerminalSemanticSnapshot snapshot() const override;
    TerminalDirtySnapshot take_delta() override;

private:
    bool start_process(std::string& error);

    Grid& terminal_grid() override;
    const Grid& terminal_grid() const override;
    HighlightTable& terminal_highlights() override;
    const HighlightTable& terminal_highlights() const override;
    void terminal_resize_grid(int cols, int rows) override;
    bool terminal_write_process(std::string_view bytes) override;
    void terminal_mark_activity() override;
    void terminal_set_title(std::string_view title) override;
    std::string terminal_read_clipboard() const override;
    void terminal_write_clipboard(std::string_view text) override;
    void terminal_set_cursor_position(
        int col, int row, TerminalCursorBlinkUpdate blink_update) override;
    std::pair<int, int> terminal_published_cursor_position() const override;
    void terminal_set_cursor_display_override(
        std::optional<std::pair<int, int>> position) override;
    void terminal_set_cursor_style(
        CursorShape shape, bool blink, bool visible) override;
    void terminal_begin_cursor_publish_batch() override;
    void terminal_end_cursor_publish_batch() override;
    void terminal_line_scrolled_off(int row) override;
    void terminal_mouse_mode_changed(int mode, bool enable) override;
    void terminal_collect_extra_attr_ids(
        std::unordered_map<uint16_t, HlAttr>& active_attrs) override;
    void terminal_remap_extra_highlight_ids(
        const std::function<uint16_t(uint16_t)>& remap_fn) override;

    Grid grid_;
    HighlightTable highlights_;
    TerminalCore core_;
#ifdef _WIN32
    ConPtyProcess process_;
#else
    UnixPtyProcess process_;
#endif
    std::string clipboard_;
    std::string published_title_;
    std::pair<int, int> published_cursor_{ 0, 0 };
    std::optional<std::pair<int, int>> cursor_override_;
    CursorShape cursor_shape_ = CursorShape::Block;
    bool cursor_blink_ = false;
    bool cursor_visible_ = true;
};

} // namespace draxul
