#include <catch2/catch_all.hpp>
#include <draxul/remote_terminal_protocol.h>
#include <draxul/terminal_core.h>

#include <nlohmann/json.hpp>
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

TEST_CASE("terminal core replaces invalid UTF-8 before snapshots reach the wire",
    "[terminal-core][unicode][server]")
{
    CoreHarness harness(12, 2);
    std::string output;
    output.push_back(static_cast<char>(0x80));
    output.push_back(static_cast<char>(0xFF));
    output += "\xE2\x82X"; // Truncated U+20AC followed by printable text.
    output += "\xC3\xA9"; // Valid U+00E9.
    harness.core.feed(output);

    const auto snapshot = harness.core.semantic_snapshot();
    static constexpr std::string_view kReplacement = "\xEF\xBF\xBD";
    REQUIRE(snapshot.cells.size() >= 6);
    CHECK(snapshot.cells[0].text == kReplacement);
    CHECK(snapshot.cells[1].text == kReplacement);
    CHECK(snapshot.cells[2].text == kReplacement);
    CHECK(snapshot.cells[3].text == kReplacement);
    CHECK(snapshot.cells[4].text == "X");
    CHECK(snapshot.cells[5].text == "\xC3\xA9");

    const RemoteTerminalEvent event{
        .kind = RemoteTerminalEventKind::Snapshot,
        .version = {
            .server_epoch = "utf8-test",
            .terminal_id = "terminal",
            .generation = 1,
            .sequence = 0,
        },
        .snapshot = snapshot,
    };
    CHECK_NOTHROW(remote_terminal_event_to_json(event).dump());
}

TEST_CASE("terminal core bounds wire-visible titles on a UTF-8 boundary",
    "[terminal-core][metadata][resource-bounds]")
{
    CoreHarness harness(12, 2);
    std::string title(
        TerminalStateLimits::kMaxTitleBytes - 1, 'x');
    title += "\xC3\xA9";
    harness.core.feed(
        std::string("\x1B]0;") + title + "\x07");

    const auto snapshot = harness.core.semantic_snapshot();
    CHECK(snapshot.metadata.title.size()
        == TerminalStateLimits::kMaxTitleBytes - 1);
    CHECK(snapshot.metadata.title
        == std::string(
            TerminalStateLimits::kMaxTitleBytes - 1, 'x'));
    CHECK(harness.published_title
        == snapshot.metadata.title);
}

TEST_CASE("terminal core bounds shell marks and prunes rows on resize",
    "[terminal-core][metadata][resource-bounds]")
{
    CoreHarness harness(8, 4);
    for (size_t index = 0;
         index < TerminalStateLimits::kMaxShellMarks + 32;
         ++index)
    {
        harness.core.feed("\x1B]133;A\x07");
    }
    CHECK(harness.core.semantic_snapshot()
              .metadata.shell_marks.size()
        == TerminalStateLimits::kMaxShellMarks);

    CoreHarness resized(8, 4);
    resized.core.feed("\x1B[4;1H\x1B]133;A\x07");
    REQUIRE(resized.core.semantic_snapshot()
                .metadata.shell_marks.size()
        == 1);
    resized.core.resize(8, 2);
    CHECK(resized.core.semantic_snapshot()
              .metadata.shell_marks.empty());
}

TEST_CASE("terminal core reset clears process metadata",
    "[terminal-core][metadata][restart]")
{
    CoreHarness harness(8, 3);
    harness.core.feed(
        "\x1B]0;old title\x07"
        "\x1B]7;file:///old/path\x07"
        "\x1B]133;A\x07");
    REQUIRE_FALSE(
        harness.core.semantic_snapshot()
            .metadata.title.empty());
    REQUIRE_FALSE(
        harness.core.semantic_snapshot()
            .metadata.working_directory.empty());

    harness.core.reset();
    const auto snapshot
        = harness.core.semantic_snapshot();
    CHECK(snapshot.metadata.title.empty());
    CHECK(snapshot.metadata.working_directory.empty());
    CHECK(snapshot.metadata.shell_marks.empty());
}

TEST_CASE("terminal core shifts and prunes shell marks with live rows",
    "[terminal-core][metadata][scroll]")
{
    CoreHarness harness(8, 3);
    harness.core.feed(
        "\x1B[2;1H\x1B]133;A\x07"
        "\x1B[3;1H\n");
    auto marks = harness.core.semantic_snapshot()
                     .metadata.shell_marks;
    REQUIRE(marks.size() == 1);
    CHECK(marks[0].row == 0);

    harness.core.feed("\x1B[3;1H\n");
    CHECK(harness.core.semantic_snapshot()
              .metadata.shell_marks.empty());

    CoreHarness inserted(8, 3);
    inserted.core.feed(
        "\x1B[2;1H\x1B]133;A\x07"
        "\x1B[2;1H\x1B[1L");
    marks = inserted.core.semantic_snapshot()
                .metadata.shell_marks;
    REQUIRE(marks.size() == 1);
    CHECK(marks[0].row == 2);
}

TEST_CASE("terminal core prunes shell marks erased from the display",
    "[terminal-core][metadata][erase]")
{
    const auto populate_marks = [](CoreHarness& harness) {
        harness.core.feed(
            "\x1B[1;1H\x1B]133;A\x07"
            "\x1B[2;2H\x1B]133;B\x07"
            "\x1B[3;1H\x1B]133;C\x07");
    };

    CoreHarness below(8, 3);
    populate_marks(below);
    below.core.feed("\x1B[2;2H\x1B[0J");
    auto marks = below.core.semantic_snapshot()
                     .metadata.shell_marks;
    REQUIRE(marks.size() == 2);
    CHECK(marks[0].row == 0);
    CHECK(marks[1].row == 1);

    CoreHarness above(8, 3);
    populate_marks(above);
    above.core.feed("\x1B[2;2H\x1B[1J");
    marks = above.core.semantic_snapshot()
                .metadata.shell_marks;
    REQUIRE(marks.size() == 2);
    CHECK(marks[0].row == 1);
    CHECK(marks[1].row == 2);

    CoreHarness all(8, 3);
    populate_marks(all);
    all.core.feed("\x1B[2J");
    CHECK(all.core.semantic_snapshot()
              .metadata.shell_marks.empty());
}
