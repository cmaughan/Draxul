#include <draxul/kanban/kanban_board.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>

namespace draxul::kanban
{
namespace
{
bool ends_with(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size()
        && text.substr(text.size() - suffix.size()) == suffix;
}

size_t sequence_end(std::string_view file_name)
{
    size_t end = 0;
    while(end < file_name.size()
        && std::isdigit(static_cast<unsigned char>(file_name[end])))
        ++end;
    return end > 0 && end < file_name.size() && file_name[end] == ' '
        ? end
        : std::string_view::npos;
}

int preferred_column_rank(std::string_view name)
{
    static constexpr std::array<std::string_view, 3> kPreferredOrder{
        "ice-box",
        "pending",
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
    if(ends_with(file_name, "-test.md"))
    {
        return CardKind::Test;
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
    case CardKind::Test:
        return "\xF0\x9F\xA7\xAA";
    case CardKind::Note:
    default:
        return "\xF0\x9F\x93\x84";
    }
}

std::string icon_for_priority(int priority)
{
    switch(priority)
    {
    case 0:
        return "🚨";
    case 1:
        return "🔥";
    case 2:
        return "⚡";
    case 3:
        return "🔹";
    default:
        return "·";
    }
}

std::string card_display_name(std::string_view file_name)
{
    if(ends_with(file_name, ".md"))
        file_name.remove_suffix(3);

    static constexpr std::array<std::string_view, 4> kind_suffixes{
        "-bug", "-feature", "-refactor", "-test",
    };
    for(const auto suffix : kind_suffixes)
    {
        if(ends_with(file_name, suffix))
        {
            file_name.remove_suffix(suffix.size());
            break;
        }
    }
    while(!file_name.empty() && file_name.back() == ' ')
        file_name.remove_suffix(1);

    if(const size_t number_end = sequence_end(file_name);
        number_end != std::string_view::npos)
    {
        file_name.remove_prefix(number_end + 1);
        while(!file_name.empty() && file_name.front() == ' ')
            file_name.remove_prefix(1);
    }
    return std::string(file_name);
}

std::optional<std::string> card_sequence_number(std::string_view file_name)
{
    const size_t end = sequence_end(file_name);
    if(end == std::string_view::npos)
        return std::nullopt;
    return std::string(file_name.substr(0, end));
}

void sort_cards_by_priority(std::vector<KanbanCard>& cards)
{
    std::stable_sort(cards.begin(), cards.end(), [](const KanbanCard& lhs, const KanbanCard& rhs) {
        return lhs.priority.value_or(std::numeric_limits<int>::max())
            < rhs.priority.value_or(std::numeric_limits<int>::max());
    });
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

void arrange_standard_columns(std::vector<KanbanColumn>& columns)
{
    std::stable_sort(columns.begin(), columns.end(),
        [](const KanbanColumn& lhs, const KanbanColumn& rhs) {
            return preferred_column_rank(lhs.name) < preferred_column_rank(rhs.name);
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
