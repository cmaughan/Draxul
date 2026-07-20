#include <draxul/kanban/kanban_layout.h>

#include <draxul/unicode.h>

#include <algorithm>

namespace draxul::kanban
{
namespace
{

int visible_card_rows_for_grid(int grid_rows)
{
    return std::max(0, grid_rows - 4);
}

int max_card_count(const KanbanBoard& board)
{
    int result = 0;
    for (const auto& column : board.columns)
        result = std::max(result, static_cast<int>(column.cards.size()));
    return result;
}

bool is_valid_column(const KanbanBoard& board, std::optional<int> column)
{
    return column && *column >= 0 && *column < static_cast<int>(board.columns.size());
}

// Scrollable content height: a zoomed single column scrolls over its own cards,
// otherwise scrolling spans the tallest column.
int content_rows_for(const KanbanBoard& board, std::optional<int> zoom_column)
{
    if (is_valid_column(board, zoom_column))
        return static_cast<int>(board.columns[static_cast<size_t>(*zoom_column)].cards.size());
    return max_card_count(board);
}

int text_cell_width(std::string_view text)
{
    return draxul::display_cell_width(text);
}

std::string dots(int count)
{
    return std::string(static_cast<size_t>(std::max(0, count)), '.');
}

} // namespace

KanbanLayout layout_kanban_board(const KanbanBoard& board, KanbanSelection selection, const KanbanLayoutOptions& options)
{
    KanbanLayout layout;
    layout.visible_card_rows = visible_card_rows_for_grid(options.grid_rows);
    layout.content_rows = content_rows_for(board, options.zoom_column);

    const int column_count = static_cast<int>(board.columns.size());
    if (column_count <= 0)
        return layout;

    const bool zoomed = is_valid_column(board, options.zoom_column);
    if (zoomed)
    {
        // Single column spanning the whole width.
        layout.columns.push_back(KanbanColumnLayout{
            .x = 0,
            .width = std::max(0, options.grid_cols),
            .index = *options.zoom_column,
        });
    }
    else
    {
        layout.columns.reserve(board.columns.size());
        const int base_width = options.grid_cols / column_count;
        int x = 0;
        for (int column = 0; column < column_count; ++column)
        {
            const bool is_last = column == column_count - 1;
            const int width = is_last ? std::max(0, options.grid_cols - x) : std::max(0, base_width);
            layout.columns.push_back(KanbanColumnLayout{
                .x = x,
                .width = width,
                .index = column,
            });
            x += width;
        }
    }

    const int scroll_row = std::max(0, options.scroll_row);
    const int visible_end = scroll_row + layout.visible_card_rows;
    for (const auto& column_layout : layout.columns)
    {
        if (column_layout.width <= 0)
            continue;

        const int column = column_layout.index;
        const auto& cards = board.columns[static_cast<size_t>(column)].cards;
        for (int card = 0; card < static_cast<int>(cards.size()); ++card)
        {
            if (card < scroll_row || card >= visible_end)
                continue;

            layout.rows.push_back(KanbanCardRowLayout{
                .column = column,
                .card = card,
                .x = column_layout.x + 1,
                .y = 3 + (card - scroll_row),
                .width = std::max(1, column_layout.width - 2),
                .selected = column == selection.column && card == selection.card,
            });
        }
    }

    return layout;
}

std::string truncate_to_cells(std::string_view text, int max_cells)
{
    if (max_cells <= 0)
        return {};

    if (text_cell_width(text) <= max_cells)
        return std::string(text);

    if (max_cells <= 3)
        return dots(max_cells);

    const int payload_cells = max_cells - 3;
    std::string result;
    int used_cells = 0;
    for (const auto& cluster : draxul::display_clusters(text))
    {
        if (used_cells + cluster.cell_width > payload_cells)
            break;

        result.append(cluster.text);
        used_cells += cluster.cell_width;
    }

    result += "...";
    return result;
}

int next_scroll_row_for_selection(const KanbanBoard& board, KanbanSelection selection, int current_scroll_row,
    int grid_rows, std::optional<int> zoom_column)
{
    const int visible_rows = visible_card_rows_for_grid(grid_rows);
    if (visible_rows <= 0)
        return 0;

    const int content_rows = content_rows_for(board, zoom_column);
    const int max_scroll = std::max(0, content_rows - visible_rows);
    const int current = std::clamp(current_scroll_row, 0, max_scroll);

    if (selection.card < current)
        return std::clamp(selection.card, 0, max_scroll);
    if (selection.card >= current + visible_rows)
        return std::clamp(selection.card - visible_rows + 1, 0, max_scroll);
    return current;
}

} // namespace draxul::kanban
