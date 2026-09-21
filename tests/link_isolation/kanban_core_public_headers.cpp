#include <draxul/kanban/kanban_board.h>
#include <draxul/kanban/kanban_layout.h>
#include <draxul/kanban/kanban_navigation.h>
#include <draxul/kanban/kanban_store.h>

#include <string>

int main()
{
    draxul::kanban::KanbanBoard board;
    draxul::kanban::KanbanSelection selection;
    draxul::kanban::clamp_selection(board, selection);

    const auto layout = draxul::kanban::layout_kanban_board(
        board,
        selection,
        draxul::kanban::KanbanLayoutOptions{});

    draxul::kanban::KanbanNavigationState navigation;
    navigation.reset();

    std::string error;
    const auto root = draxul::kanban::resolve_kanban_root("kanban", ".", &error);
    return layout.columns.empty() && !root.empty() && error.empty() ? 0 : 1;
}
