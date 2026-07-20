#pragma once

#include <draxul/kanban/kanban_board.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul::kanban
{

struct KanbanLayoutOptions
{
    int grid_cols = 1;
    int grid_rows = 1;
    int scroll_row = 0;
    // When set to a valid column index, only that column is laid out and it
    // spans the full grid width (single-column "zoom" view). Nullopt lays out
    // every column side by side.
    std::optional<int> zoom_column;
};

struct KanbanColumnLayout
{
    int x = 0;
    int width = 0;
    int index = 0;
};

struct KanbanCardRowLayout
{
    int column = 0;
    int card = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    bool selected = false;
};

struct KanbanLayout
{
    std::vector<KanbanColumnLayout> columns;
    std::vector<KanbanCardRowLayout> rows;
    int visible_card_rows = 0;
    int content_rows = 0;
};

KanbanLayout layout_kanban_board(const KanbanBoard& board, KanbanSelection selection, const KanbanLayoutOptions& options);
std::string truncate_to_cells(std::string_view text, int max_cells);
// Computes the scroll row that keeps `selection` visible. When `zoom_column`
// is set, the scrollable content is that single column's card count rather than
// the tallest column, so a zoomed short column cannot scroll past its end.
int next_scroll_row_for_selection(const KanbanBoard& board, KanbanSelection selection, int current_scroll_row,
    int grid_rows, std::optional<int> zoom_column = std::nullopt);

} // namespace draxul::kanban
