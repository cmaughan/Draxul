#include <catch2/catch_all.hpp>

#include <draxul/kanban/kanban_board.h>
#include <draxul/kanban/kanban_layout.h>

using namespace draxul::kanban;

namespace
{

KanbanBoard make_board()
{
    KanbanBoard board;
    board.columns = {
        KanbanColumn{
            .name = "todo",
            .cards = {
                KanbanCard{ .file_name = "one.md" },
                KanbanCard{ .file_name = "two.md" },
                KanbanCard{ .file_name = "three.md" },
            },
        },
        KanbanColumn{
            .name = "doing",
            .cards = {
                KanbanCard{ .file_name = "active.md" },
            },
        },
        KanbanColumn{
            .name = "done",
            .cards = {
                KanbanCard{ .file_name = "shipped.md" },
                KanbanCard{ .file_name = "closed.md" },
            },
        },
    };
    return board;
}

} // namespace

TEST_CASE("kanban layout distributes equal width columns with remainder on last column", "[kanban][layout]")
{
    const auto board = make_board();
    const auto layout = layout_kanban_board(board, KanbanSelection{ .column = 0, .card = 0 },
        KanbanLayoutOptions{ .grid_cols = 91, .grid_rows = 12 });

    REQUIRE(layout.columns.size() == 3);
    REQUIRE(layout.columns[0].x == 0);
    REQUIRE(layout.columns[0].width == 30);
    REQUIRE(layout.columns[1].x == 30);
    REQUIRE(layout.columns[1].width == 30);
    REQUIRE(layout.columns[2].x == 60);
    REQUIRE(layout.columns[2].width == 31);
    REQUIRE(layout.visible_card_rows == 8);
    REQUIRE(layout.content_rows == 3);
}

TEST_CASE("kanban layout positions visible card rows and marks selection", "[kanban][layout]")
{
    const auto board = make_board();
    const auto layout = layout_kanban_board(board, KanbanSelection{ .column = 0, .card = 2 },
        KanbanLayoutOptions{ .grid_cols = 30, .grid_rows = 6, .scroll_row = 1 });

    REQUIRE(layout.visible_card_rows == 2);
    REQUIRE(layout.rows.size() == 3);

    const auto selected = std::ranges::find_if(layout.rows, [](const KanbanCardRowLayout& row) {
        return row.selected;
    });
    REQUIRE(selected != layout.rows.end());
    REQUIRE(selected->column == 0);
    REQUIRE(selected->card == 2);
    REQUIRE(selected->x == 1);
    REQUIRE(selected->y == 3 + (2 - 1));
    REQUIRE(selected->width == 8);
}

TEST_CASE("kanban layout omits rows for zero-width columns in narrow grids", "[kanban][layout]")
{
    const auto board = make_board();
    const auto layout = layout_kanban_board(board, KanbanSelection{ .column = 0, .card = 0 },
        KanbanLayoutOptions{ .grid_cols = 2, .grid_rows = 6 });

    REQUIRE(layout.columns.size() == 3);
    REQUIRE(layout.columns[0].width == 0);
    REQUIRE(layout.columns[1].width == 0);
    REQUIRE(layout.columns[2].width == 2);
    REQUIRE(std::ranges::none_of(layout.rows, [](const KanbanCardRowLayout& row) {
        return row.column == 0 || row.column == 1;
    }));
    REQUIRE(std::ranges::all_of(layout.rows, [](const KanbanCardRowLayout& row) {
        return row.column == 2 && row.x == 1 && row.width == 1;
    }));
}

TEST_CASE("kanban layout zooms to a single full-width column", "[kanban][layout]")
{
    const auto board = make_board();
    const auto layout = layout_kanban_board(board, KanbanSelection{ .column = 2, .card = 1 },
        KanbanLayoutOptions{ .grid_cols = 91, .grid_rows = 12, .zoom_column = 2 });

    REQUIRE(layout.columns.size() == 1);
    REQUIRE(layout.columns[0].x == 0);
    REQUIRE(layout.columns[0].width == 91);
    REQUIRE(layout.columns[0].index == 2);

    // Scrollable content and rows come from the zoomed column only.
    REQUIRE(layout.content_rows == 2);
    REQUIRE(layout.rows.size() == 2);
    REQUIRE(std::ranges::all_of(layout.rows, [](const KanbanCardRowLayout& row) {
        return row.column == 2 && row.x == 1 && row.width == 89;
    }));

    const auto selected = std::ranges::find_if(layout.rows, [](const KanbanCardRowLayout& row) {
        return row.selected;
    });
    REQUIRE(selected != layout.rows.end());
    REQUIRE(selected->card == 1);
}

TEST_CASE("kanban layout zoom scrolls within the zoomed column", "[kanban][layout]")
{
    KanbanBoard board;
    KanbanColumn tall{ .name = "todo" };
    for (int i = 0; i < 6; ++i)
        tall.cards.push_back(KanbanCard{ .file_name = "card-" + std::to_string(i) + ".md" });
    board.columns = { std::move(tall) };

    const auto layout = layout_kanban_board(board, KanbanSelection{ .column = 0, .card = 4 },
        KanbanLayoutOptions{ .grid_cols = 40, .grid_rows = 6, .scroll_row = 3, .zoom_column = 0 });

    REQUIRE(layout.visible_card_rows == 2);
    REQUIRE(layout.content_rows == 6);
    REQUIRE(layout.rows.size() == 2);
    // Cards 3 and 4 are the visible window; card 3 sits at the first card row.
    REQUIRE(layout.rows.front().card == 3);
    REQUIRE(layout.rows.front().y == 3);
    REQUIRE(layout.rows.back().card == 4);
    REQUIRE(layout.rows.back().y == 4);
}

TEST_CASE("kanban layout ignores an out-of-range zoom column", "[kanban][layout]")
{
    const auto board = make_board();
    const auto layout = layout_kanban_board(board, KanbanSelection{ .column = 0, .card = 0 },
        KanbanLayoutOptions{ .grid_cols = 91, .grid_rows = 12, .zoom_column = 7 });

    // An invalid zoom index falls back to the normal multi-column layout.
    REQUIRE(layout.columns.size() == 3);
    REQUIRE(layout.content_rows == 3);
}

TEST_CASE("kanban truncation uses cell widths and ascii ellipsis", "[kanban][layout]")
{
    REQUIRE(truncate_to_cells("abcdef", 6) == "abcdef");
    REQUIRE(truncate_to_cells("abcdef", 5) == "ab...");
    REQUIRE(truncate_to_cells("abcdef", 3) == "...");
    REQUIRE(truncate_to_cells("abcdef", 2) == "..");
    REQUIRE(truncate_to_cells("abcdef", 0).empty());
}

TEST_CASE("kanban truncation respects wide and utf8 codepoints", "[kanban][layout]")
{
    REQUIRE(truncate_to_cells("ab\xE7\x95\x8C"
                              "cd",
                6)
        == "ab\xE7\x95\x8C"
           "cd");
    REQUIRE(truncate_to_cells("ab\xE7\x95\x8C"
                              "cd",
                5)
        == "ab...");
    REQUIRE(truncate_to_cells("\xE7\x95\x8C"
                              "abcd",
                5)
        == "\xE7\x95\x8C"
           "...");
    REQUIRE(truncate_to_cells("e\xCC\x81"
                              "abcde",
                5)
        == "e\xCC\x81"
           "a...");
    REQUIRE(truncate_to_cells("\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7"
                              "abcd",
                4)
        == "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7"
           "...");
    REQUIRE(truncate_to_cells("\xE0\xA4\x95\xE0\xA5\x8D\xE2\x80\x8D\xE0\xA4\xB7"
                              "abcd",
                4)
        == "\xE0\xA4\x95\xE0\xA5\x8D\xE2\x80\x8D\xE0\xA4\xB7"
           "...");
}

TEST_CASE("kanban scroll helper keeps the selected card visible", "[kanban][layout]")
{
    const auto board = make_board();

    REQUIRE(next_scroll_row_for_selection(board, KanbanSelection{ .column = 0, .card = 1 }, 3, 6) == 1);
    REQUIRE(next_scroll_row_for_selection(board, KanbanSelection{ .column = 0, .card = 2 }, 0, 6) == 1);
    REQUIRE(next_scroll_row_for_selection(board, KanbanSelection{ .column = 0, .card = 2 }, 1, 6) == 1);
    REQUIRE(next_scroll_row_for_selection(board, KanbanSelection{ .column = 0, .card = 1 }, -1, 6) == 0);
    REQUIRE(next_scroll_row_for_selection(board, KanbanSelection{ .column = 0, .card = 2 }, 1, 3) == 0);
}
