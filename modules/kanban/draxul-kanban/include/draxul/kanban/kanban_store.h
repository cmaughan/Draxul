#pragma once

#include <draxul/kanban/kanban_board.h>

#include <string>
#include <string_view>

namespace draxul::kanban
{

inline constexpr std::string_view kKanbanMetadataFileName = ".draxul-kanban.toml";

std::filesystem::path resolve_kanban_root(
    const std::filesystem::path& source_path,
    const std::filesystem::path& working_dir,
    std::string* error);

KanbanBoard load_kanban_board(const std::filesystem::path& root, std::string* error);
KanbanBoard load_kanban_workspace(const std::filesystem::path& root, std::string* error);
bool save_kanban_order(const KanbanBoard& board, std::string* error);
bool save_kanban_order_for_source(
    const KanbanBoard& board,
    size_t source_index,
    std::string* error);
bool reorder_card(KanbanBoard& board, KanbanSelection selection, int row_delta, std::string* error);
bool move_card_to_column(
    KanbanBoard& board,
    KanbanSelection selection,
    int target_column,
    std::string* error);

} // namespace draxul::kanban
