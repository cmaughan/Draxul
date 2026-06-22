#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace draxul::kanban
{

enum class CardKind
{
    Note,
    Bug,
    Feature,
    Refactor,
};

struct KanbanCard
{
    std::string file_name;
    std::filesystem::path path;
    CardKind kind = CardKind::Note;
};

struct KanbanColumn
{
    std::string name;
    std::filesystem::path directory;
    std::vector<KanbanCard> cards;
};

struct KanbanBoard
{
    std::filesystem::path root;
    std::vector<KanbanColumn> columns;
};

struct KanbanSelection
{
    int column = 0;
    int card = 0;
};

CardKind card_kind_for_file(std::string_view file_name);
std::string icon_for_kind(CardKind kind);
void sort_columns_for_first_load(std::vector<std::string>& names);
void clamp_selection(const KanbanBoard& board, KanbanSelection& selection);
bool selection_has_card(const KanbanBoard& board, KanbanSelection selection);
KanbanCard* selected_card(KanbanBoard& board, KanbanSelection selection);
const KanbanCard* selected_card(const KanbanBoard& board, KanbanSelection selection);

} // namespace draxul::kanban
