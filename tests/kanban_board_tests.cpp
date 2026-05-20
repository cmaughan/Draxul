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
    REQUIRE(card_kind_for_file("notes.md") == CardKind::Note);
}

TEST_CASE("kanban card icons are exact UTF-8 byte strings", "[kanban][board]")
{
    REQUIRE(icon_for_kind(CardKind::Bug) == std::string("\xF0\x9F\x90\x9B"));
    REQUIRE(icon_for_kind(CardKind::Feature) == std::string("\xE2\x9C\xA8"));
    REQUIRE(icon_for_kind(CardKind::Refactor) == std::string("\xF0\x9F\x94\xA7"));
    REQUIRE(icon_for_kind(CardKind::Note) == std::string("\xF0\x9F\x93\x84"));
}

TEST_CASE("kanban columns sort into preferred first-load order", "[kanban][board]")
{
    std::vector<std::string> names{"done", "pending", "ice-box", "review"};

    sort_columns_for_first_load(names);

    REQUIRE(names == std::vector<std::string>{"ice-box", "pending", "review", "done"});
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
