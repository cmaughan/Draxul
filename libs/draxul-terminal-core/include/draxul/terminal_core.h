#pragma once

#include <draxul/alt_screen_manager.h>
#include <draxul/attribute_cache.h>
#include <draxul/terminal_snapshot.h>
#include <draxul/vt_parser.h>
#include <draxul/vt_state.h>

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace draxul
{

enum class TerminalCursorBlinkUpdate
{
    Restart,
    Preserve,
};

struct TerminalCoreConfig
{
    bool enable_osc8_hyperlinks = true;
    bool enable_shell_integration_marks = true;
};

class ITerminalCoreHost
{
public:
    virtual ~ITerminalCoreHost() = default;

    virtual Grid& terminal_grid() = 0;
    virtual const Grid& terminal_grid() const = 0;
    virtual HighlightTable& terminal_highlights() = 0;
    virtual const HighlightTable& terminal_highlights() const = 0;
    virtual void terminal_resize_grid(int cols, int rows) = 0;

    virtual bool terminal_write_process(std::string_view bytes) = 0;
    virtual void terminal_mark_activity() = 0;
    virtual void terminal_set_title(std::string_view title) = 0;
    virtual std::string terminal_read_clipboard() const = 0;
    virtual void terminal_write_clipboard(std::string_view text) = 0;

    virtual void terminal_set_cursor_position(
        int col, int row, TerminalCursorBlinkUpdate blink_update)
        = 0;
    virtual std::pair<int, int> terminal_published_cursor_position() const = 0;
    virtual void terminal_set_cursor_display_override(
        std::optional<std::pair<int, int>> position)
        = 0;
    virtual void terminal_set_cursor_style(
        CursorShape shape, bool blink, bool visible)
        = 0;
    virtual void terminal_begin_cursor_publish_batch() = 0;
    virtual void terminal_end_cursor_publish_batch() = 0;

    virtual void terminal_line_scrolled_off(int row) = 0;
    virtual void terminal_mouse_mode_changed(int mode, bool enable) = 0;
    virtual void terminal_collect_extra_attr_ids(
        std::unordered_map<uint16_t, HlAttr>& active_attrs)
        = 0;
    virtual void terminal_remap_extra_highlight_ids(
        const std::function<uint16_t(uint16_t)>& remap_fn)
        = 0;
};

class TerminalCore
{
public:
    explicit TerminalCore(ITerminalCoreHost& host);

    void set_config(TerminalCoreConfig config)
    {
        config_ = config;
    }

    void reset();
    void feed(std::string_view bytes);
    void resize(int cols, int rows);
    void begin_output_cursor_batch();
    void end_output_cursor_batch();
    void reconcile_provisional_cursor_after_pump(bool saw_output);
    void trace_cursor_presentation_state(std::string_view stage, bool saw_output) const;
    void update_cursor_style();
    void set_logical_cursor_position(int col, int row,
        TerminalCursorBlinkUpdate blink_update = TerminalCursorBlinkUpdate::Preserve);

    const VtState& vt_state() const
    {
        return vt_;
    }

    bool bracketed_paste_mode() const
    {
        return bracketed_paste_mode_;
    }

    bool focus_reporting_mode() const
    {
        return focus_reporting_mode_;
    }

    bool synchronized_output_active() const
    {
        return synchronized_output_mode_;
    }

    const std::string& terminal_title() const
    {
        return terminal_title_;
    }

    const std::string& current_working_directory() const
    {
        return current_cwd_;
    }

    size_t attr_cache_size() const
    {
        return attr_cache_.size();
    }

    const AttributeCache& attr_cache() const
    {
        return attr_cache_;
    }

    TerminalSemanticSnapshot semantic_snapshot() const;
    TerminalDirtySnapshot dirty_snapshot() const;

private:
    enum class CharsetMode
    {
        Ascii,
        DecSpecialGraphics,
    };

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

    Grid& grid()
    {
        return host_.terminal_grid();
    }
    const Grid& grid() const
    {
        return host_.terminal_grid();
    }
    HighlightTable& highlights()
    {
        return host_.terminal_highlights();
    }
    const HighlightTable& highlights() const
    {
        return host_.terminal_highlights();
    }
    int grid_cols() const
    {
        return grid().cols();
    }
    int grid_rows() const
    {
        return grid().rows();
    }
    void set_cursor_position(
        int col, int row, TerminalCursorBlinkUpdate blink_update)
    {
        host_.terminal_set_cursor_position(col, row, blink_update);
    }
    void set_cursor_display_override(
        std::optional<std::pair<int, int>> position)
    {
        host_.terminal_set_cursor_display_override(position);
    }
    void begin_cursor_publish_batch()
    {
        host_.terminal_begin_cursor_publish_batch();
    }
    void end_cursor_publish_batch()
    {
        host_.terminal_end_cursor_publish_batch();
    }
    void on_line_scrolled_off(int row)
    {
        host_.terminal_line_scrolled_off(row);
    }
    void on_mouse_mode_changed(int mode, bool enable)
    {
        host_.terminal_mouse_mode_changed(mode, enable);
    }
    void collect_extra_attr_ids(
        std::unordered_map<uint16_t, HlAttr>& active_attrs)
    {
        host_.terminal_collect_extra_attr_ids(active_attrs);
    }
    void remap_extra_highlight_ids(
        const std::function<uint16_t(uint16_t)>& remap_fn)
    {
        host_.terminal_remap_extra_highlight_ids(remap_fn);
    }

    uint16_t attr_id();
    void compact_attr_ids();
    void clear_cell(int col, int row);
    void scroll_rows(int top, int bottom, int rows);
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
    void on_osc_cwd(const std::string& path);
    TerminalSnapshotMetadata snapshot_metadata() const;

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

    ITerminalCoreHost& host_;
    TerminalCoreConfig config_;
    HlAttr current_attr_ = {};
    AttributeCache attr_cache_;
    VtParser vt_parser_;
    VtState vt_;
    AltScreenManager alt_screen_;
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
    TerminalMouseModeSnapshot mouse_modes_;
    uint16_t current_hyperlink_id_ = 0;
    std::deque<ShellMark> shell_marks_;
    std::string terminal_title_;
    std::string current_cwd_;
};

} // namespace draxul
