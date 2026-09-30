#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
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
    Perf,
    Test,
};

struct KanbanCard
{
    std::string file_name;
    std::filesystem::path path;
    CardKind kind = CardKind::Note;
    std::optional<int> priority;
    size_t source_index = 0;
    std::string source_name;
    std::filesystem::path source_root;
};

struct KanbanSource
{
    std::string name;
    std::filesystem::path root;
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
    std::vector<KanbanSource> sources;
    std::vector<KanbanColumn> columns;
    std::vector<std::string> warnings;
};

struct KanbanSelection
{
    int column = 0;
    int card = 0;
};

CardKind card_kind_for_file(std::string_view file_name);
std::string icon_for_kind(CardKind kind);
std::string icon_for_priority(int priority);
std::optional<std::string> card_sequence_number(std::string_view file_name);
std::string card_display_name(std::string_view file_name);
void sort_cards_by_priority(std::vector<KanbanCard>& cards);
void sort_columns_for_first_load(std::vector<std::string>& names);
void arrange_standard_columns(std::vector<KanbanColumn>& columns);
void clamp_selection(const KanbanBoard& board, KanbanSelection& selection);
bool selection_has_card(const KanbanBoard& board, KanbanSelection selection);
KanbanCard* selected_card(KanbanBoard& board, KanbanSelection selection);
const KanbanCard* selected_card(const KanbanBoard& board, KanbanSelection selection);

} // namespace draxul::kanban
