#include <draxul/kanban/kanban_board.h>

#include <algorithm>
#include <array>

namespace draxul::kanban
{
namespace
{
bool ends_with(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size()
        && text.substr(text.size() - suffix.size()) == suffix;
}

int preferred_column_rank(std::string_view name)
{
    static constexpr std::array<std::string_view, 8> kPreferredOrder{
        "ice-box",
        "backlog",
        "pending",
        "todo",
        "doing",
        "in-progress",
        "review",
        "done",
    };

    const auto it = std::ranges::find(kPreferredOrder, name);
    if(it == kPreferredOrder.end())
    {
        return static_cast<int>(kPreferredOrder.size());
    }
    return static_cast<int>(std::distance(kPreferredOrder.begin(), it));
}
} // namespace

CardKind card_kind_for_file(std::string_view file_name)
{
    if(ends_with(file_name, "-bug.md"))
    {
        return CardKind::Bug;
    }
    if(ends_with(file_name, "-feature.md"))
    {
        return CardKind::Feature;
    }
    if(ends_with(file_name, "-refactor.md"))
    {
        return CardKind::Refactor;
    }
    return CardKind::Note;
}

std::string icon_for_kind(CardKind kind)
{
    switch(kind)
    {
    case CardKind::Bug:
        return "\xF0\x9F\x90\x9B";
    case CardKind::Feature:
        return "\xE2\x9C\xA8";
    case CardKind::Refactor:
        return "\xF0\x9F\x94\xA7";
    case CardKind::Note:
    default:
        return "\xF0\x9F\x93\x84";
    }
}

void sort_columns_for_first_load(std::vector<std::string>& names)
{
    std::ranges::sort(names, [](const std::string& lhs, const std::string& rhs) {
        const auto lhs_rank = preferred_column_rank(lhs);
        const auto rhs_rank = preferred_column_rank(rhs);
        if(lhs_rank != rhs_rank)
        {
            return lhs_rank < rhs_rank;
        }
        return lhs < rhs;
    });
}

void clamp_selection(const KanbanBoard& board, KanbanSelection& selection)
{
    if(board.columns.empty())
    {
        selection.column = 0;
        selection.card = 0;
        return;
    }

    selection.column = std::clamp(selection.column, 0, static_cast<int>(board.columns.size()) - 1);
    const auto& cards = board.columns[static_cast<size_t>(selection.column)].cards;
    if(cards.empty())
    {
        selection.card = 0;
        return;
    }
    selection.card = std::clamp(selection.card, 0, static_cast<int>(cards.size()) - 1);
}

bool selection_has_card(const KanbanBoard& board, KanbanSelection selection)
{
    if(selection.column < 0 || selection.card < 0)
    {
        return false;
    }
    const auto column = static_cast<size_t>(selection.column);
    if(column >= board.columns.size())
    {
        return false;
    }
    const auto card = static_cast<size_t>(selection.card);
    return card < board.columns[column].cards.size();
}

KanbanCard* selected_card(KanbanBoard& board, KanbanSelection selection)
{
    if(!selection_has_card(board, selection))
    {
        return nullptr;
    }
    return &board.columns[static_cast<size_t>(selection.column)].cards[static_cast<size_t>(selection.card)];
}

const KanbanCard* selected_card(const KanbanBoard& board, KanbanSelection selection)
{
    if(!selection_has_card(board, selection))
    {
        return nullptr;
    }
    return &board.columns[static_cast<size_t>(selection.column)].cards[static_cast<size_t>(selection.card)];
}

} // namespace draxul::kanban
