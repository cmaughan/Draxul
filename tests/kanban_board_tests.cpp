#include <catch2/catch_all.hpp>

#include <draxul/kanban/kanban_board.h>

#include <string>
#include <vector>

using namespace draxul::kanban;

TEST_CASE("kanban card kind is inferred from file suffix", "[kanban][board]")
{
    REQUIRE(card_kind_for_file("login-bug.md") == CardKind::Bug);
    REQUIRE(card_kind_for_file("editor-feature.md") == CardKind::Feature);
    REQUIRE(card_kind_for_file("grid-refactor.md") == CardKind::Refactor);
    REQUIRE(card_kind_for_file("frame-upload-perf.md") == CardKind::Perf);
    REQUIRE(card_kind_for_file("safety-test.md") == CardKind::Test);
    REQUIRE(card_kind_for_file("notes.md") == CardKind::Note);
}

TEST_CASE("kanban card icons are exact UTF-8 byte strings", "[kanban][board]")
{
    REQUIRE(icon_for_kind(CardKind::Bug) == std::string("\xF0\x9F\x90\x9B"));
    REQUIRE(icon_for_kind(CardKind::Feature) == std::string("\xE2\x9C\xA8"));
    REQUIRE(icon_for_kind(CardKind::Refactor) == std::string("\xF0\x9F\x94\xA7"));
    REQUIRE(icon_for_kind(CardKind::Perf) == std::string("\xF0\x9F\x93\x88"));
    REQUIRE(icon_for_kind(CardKind::Test) == std::string("\xF0\x9F\xA7\xAA"));
    REQUIRE(icon_for_kind(CardKind::Note) == std::string("\xF0\x9F\x93\x84"));
}

TEST_CASE("kanban card labels omit sequence, type and extension only in the view", "[kanban][board]")
{
    REQUIRE(card_display_name("01 plugin-storage-exclusive-temporary-files -bug.md")
        == "plugin-storage-exclusive-temporary-files");
    REQUIRE(card_display_name("134 default-kanban-column-order -feature.md")
        == "default-kanban-column-order");
    REQUIRE(card_display_name("32 ligature-dirty-run-sweep -perf.md")
        == "ligature-dirty-run-sweep");
    REQUIRE(card_display_name("safety-test.md") == "safety");
    REQUIRE(card_display_name("2026-summary.md") == "2026-summary");
    REQUIRE(card_display_name("notes.md") == "notes");
    REQUIRE(card_sequence_number("01 plugin-storage -bug.md") == "01");
    REQUIRE_FALSE(card_sequence_number("2026-summary.md").has_value());
    REQUIRE(icon_for_priority(1) == "🔥");
    REQUIRE(icon_for_priority(2) == "⚡");
    REQUIRE(icon_for_priority(3) == "🔹");
}

TEST_CASE("kanban priorities sort ahead of unranked cards and retain saved order within each rank",
    "[kanban][board]")
{
    std::vector<KanbanCard> cards{
        KanbanCard{.file_name = "unranked.md"},
        KanbanCard{.file_name = "p2.md", .priority = 2},
        KanbanCard{.file_name = "first-p1.md", .priority = 1},
        KanbanCard{.file_name = "second-p1.md", .priority = 1},
    };
    sort_cards_by_priority(cards);
    REQUIRE(cards[0].file_name == "first-p1.md");
    REQUIRE(cards[1].file_name == "second-p1.md");
    REQUIRE(cards[2].file_name == "p2.md");
    REQUIRE(cards[3].file_name == "unranked.md");
}

TEST_CASE("kanban columns sort into preferred first-load order", "[kanban][board]")
{
    std::vector<std::string> names{"done", "review", "pending", "backlog", "ice-box"};

    sort_columns_for_first_load(names);

    REQUIRE(names == std::vector<std::string>{"ice-box", "pending", "done", "backlog", "review"});
}

TEST_CASE("kanban standard columns precede custom columns without reordering them",
    "[kanban][board]")
{
    std::vector<KanbanColumn> columns{
        KanbanColumn{.name = "review"},
        KanbanColumn{.name = "done"},
        KanbanColumn{.name = "backlog"},
        KanbanColumn{.name = "ice-box"},
        KanbanColumn{.name = "pending"},
    };

    arrange_standard_columns(columns);

    REQUIRE(std::vector<std::string>{
                columns[0].name,
                columns[1].name,
                columns[2].name,
                columns[3].name,
                columns[4].name,
            }
        == std::vector<std::string>{"ice-box", "pending", "done", "review", "backlog"});
}

TEST_CASE("kanban selection clamps to existing columns and cards", "[kanban][board]")
{
    KanbanBoard board;
    board.columns.push_back(KanbanColumn{.name = "pending"});
    board.columns.push_back(KanbanColumn{
        .name = "done",
        .cards = {KanbanCard{.file_name = "a.md"}, KanbanCard{.file_name = "b.md"}}});

    KanbanSelection selection{.column = 5, .card = 7};

    clamp_selection(board, selection);

    REQUIRE(selection.column == 1);
    REQUIRE(selection.card == 1);
    REQUIRE(selection_has_card(board, selection));
    REQUIRE(selected_card(board, selection)->file_name == "b.md");

    selection = KanbanSelection{.column = -3, .card = -9};
    clamp_selection(board, selection);

    REQUIRE(selection.column == 0);
    REQUIRE(selection.card == 0);
    REQUIRE_FALSE(selection_has_card(board, selection));
    REQUIRE(selected_card(board, selection) == nullptr);
}
