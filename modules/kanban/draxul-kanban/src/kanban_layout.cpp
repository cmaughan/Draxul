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

bool is_indic_virama(uint32_t cp)
{
    switch (cp)
    {
    case 0x094D:
    case 0x09CD:
    case 0x0A4D:
    case 0x0ACD:
    case 0x0B4D:
    case 0x0BCD:
    case 0x0C4D:
    case 0x0CCD:
    case 0x0D4D:
    case 0x0DCA:
        return true;
    default:
        return false;
    }
}

size_t next_cluster_end(std::string_view text, size_t offset)
{
    uint32_t base = 0;
    if (!draxul::utf8_decode_next(text, offset, base))
        return offset;

    bool consumed_regional_pair = false;
    bool previous_was_virama = false;
    while (offset < text.size())
    {
        const size_t next_start = offset;
        uint32_t cp = 0;
        if (!draxul::utf8_decode_next(text, offset, cp))
            return offset;

        if (cp == 0x200D && offset < text.size())
        {
            uint32_t joined = 0;
            draxul::utf8_decode_next(text, offset, joined);
            previous_was_virama = false;
            continue;
        }

        if (previous_was_virama)
        {
            previous_was_virama = false;
            continue;
        }

        if (is_indic_virama(cp))
        {
            previous_was_virama = true;
            continue;
        }

        if (draxul::is_width_ignorable(cp) || draxul::is_emoji_modifier(cp))
            continue;

        if (!consumed_regional_pair && draxul::is_regional_indicator(base) && draxul::is_regional_indicator(cp))
        {
            consumed_regional_pair = true;
            continue;
        }

        return next_start;
    }

    return offset;
}

int text_cell_width(std::string_view text)
{
    int width = 0;
    size_t offset = 0;
    while (offset < text.size())
    {
        const size_t cluster_start = offset;
        offset = next_cluster_end(text, offset);
        width += draxul::cluster_cell_width(text.substr(cluster_start, offset - cluster_start));
    }
    return width;
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
    layout.content_rows = max_card_count(board);

    const int column_count = static_cast<int>(board.columns.size());
    if (column_count <= 0)
        return layout;

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

    const int scroll_row = std::max(0, options.scroll_row);
    const int visible_end = scroll_row + layout.visible_card_rows;
    for (int column = 0; column < column_count; ++column)
    {
        const auto& cards = board.columns[static_cast<size_t>(column)].cards;
        const auto& column_layout = layout.columns[static_cast<size_t>(column)];
        if (column_layout.width <= 0)
            continue;

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
    size_t offset = 0;
    while (offset < text.size())
    {
        const size_t cluster_start = offset;
        offset = next_cluster_end(text, offset);

        const auto cluster = text.substr(cluster_start, offset - cluster_start);
        const int cluster_width = draxul::cluster_cell_width(cluster);
        if (used_cells + cluster_width > payload_cells)
            break;

        result.append(cluster);
        used_cells += cluster_width;
    }

    result += "...";
    return result;
}

int next_scroll_row_for_selection(const KanbanBoard& board, KanbanSelection selection, int current_scroll_row, int grid_rows)
{
    const int visible_rows = visible_card_rows_for_grid(grid_rows);
    if (visible_rows <= 0)
        return 0;

    const int content_rows = max_card_count(board);
    const int max_scroll = std::max(0, content_rows - visible_rows);
    const int current = std::clamp(current_scroll_row, 0, max_scroll);

    if (selection.card < current)
        return std::clamp(selection.card, 0, max_scroll);
    if (selection.card >= current + visible_rows)
        return std::clamp(selection.card - visible_rows + 1, 0, max_scroll);
    return current;
}

} // namespace draxul::kanban
