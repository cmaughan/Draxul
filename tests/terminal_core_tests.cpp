#include <catch2/catch_all.hpp>
#include <draxul/terminal_core.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

using namespace draxul;

namespace
{

class CoreHarness final : public ITerminalCoreHost
{
public:
    CoreHarness(int cols, int rows)
        : core(*this)
    {
        grid.resize(cols, rows);
        core.reset();
    }

    Grid& terminal_grid() override
    {
        return grid;
    }
    const Grid& terminal_grid() const override
    {
        return grid;
    }
    HighlightTable& terminal_highlights() override
    {
        return highlights;
    }
    const HighlightTable& terminal_highlights() const override
    {
        return highlights;
    }
    void terminal_resize_grid(int cols, int rows) override
    {
        grid.resize(cols, rows);
    }

    bool terminal_write_process(std::string_view bytes) override
    {
        process_writes += bytes;
        return true;
    }
    void terminal_mark_activity() override
    {
        ++activity_count;
    }
    void terminal_set_title(std::string_view value) override
    {
        published_title = value;
    }
    std::string terminal_read_clipboard() const override
    {
        return clipboard;
    }
    void terminal_write_clipboard(std::string_view value) override
    {
        clipboard = value;
    }

    void terminal_set_cursor_position(
        int col, int row, TerminalCursorBlinkUpdate) override
    {
        cursor = { col, row };
    }
    std::pair<int, int> terminal_published_cursor_position() const override
    {
        return cursor;
    }
    void terminal_set_cursor_display_override(
        std::optional<std::pair<int, int>> position) override
    {
        cursor_override = position;
    }
    void terminal_set_cursor_style(
        CursorShape shape, bool blink, bool visible) override
    {
        cursor_shape = shape;
        cursor_blink = blink;
        cursor_visible = visible;
    }
    void terminal_begin_cursor_publish_batch() override {}
    void terminal_end_cursor_publish_batch() override {}

    void terminal_line_scrolled_off(int) override {}
    void terminal_mouse_mode_changed(int mode, bool enable) override
    {
        mouse_mode_changes.emplace_back(mode, enable);
    }
    void terminal_collect_extra_attr_ids(
        std::unordered_map<uint16_t, HlAttr>&) override
    {
    }
    void terminal_remap_extra_highlight_ids(
        const std::function<uint16_t(uint16_t)>&) override
    {
    }

    Grid grid;
    HighlightTable highlights;
    TerminalCore core;
    std::string process_writes;
    std::string clipboard;
    std::string published_title;
    std::pair<int, int> cursor{ 0, 0 };
    std::optional<std::pair<int, int>> cursor_override;
    CursorShape cursor_shape = CursorShape::Block;
    bool cursor_blink = false;
    bool cursor_visible = true;
    int activity_count = 0;
    std::vector<std::pair<int, bool>> mouse_mode_changes;
};

} // namespace

TEST_CASE("terminal core runs without a renderer, window, or process implementation",
    "[terminal-core][boundary]")
{
    CoreHarness harness(12, 4);
    harness.grid.clear_dirty();
    harness.core.feed(
        "\x1B]0;core title\x07"
        "\x1B[1;32mgreen\x1B[0m"
        "\x1B[?2004h"
        "\x1B[?1006h");

    const auto snapshot = harness.core.semantic_snapshot();
    CHECK(snapshot.cols == 12);
    CHECK(snapshot.rows == 4);
    CHECK(snapshot.metadata.title == "core title");
    CHECK(snapshot.metadata.modes.bracketed_paste);
    CHECK(snapshot.metadata.modes.mouse.sgr_coordinates);
    CHECK(snapshot.cells.front().text == "g");
    const auto dirty = harness.core.dirty_snapshot();
    CHECK_FALSE(dirty.full);
    CHECK(dirty.cells.size() == 5);
    CHECK(harness.published_title == "core title");
    CHECK(harness.activity_count == 1);
    const std::vector<std::pair<int, bool>> expected_mouse_modes{ { 1006, true } };
    CHECK(harness.mouse_mode_changes == expected_mouse_modes);
}

TEST_CASE("terminal core owns resize, responses, and clipboard semantics",
    "[terminal-core][boundary]")
{
    CoreHarness harness(8, 3);
    harness.core.feed("\x1B[6n\x1B]52;c;Y29yZQ==\x1B\\");
    harness.core.resize(10, 5);

    CHECK(harness.process_writes == "\x1B[1;1R");
    CHECK(harness.clipboard == "core");
    CHECK(harness.grid.cols() == 10);
    CHECK(harness.grid.rows() == 5);
    const TerminalCursorSnapshot expected_cursor{ .col = 0, .row = 0 };
    CHECK(harness.core.semantic_snapshot().metadata.cursor == expected_cursor);
}
