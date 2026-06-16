# Kanban Board

> 27 nodes

## Key Concepts

- **move_card()** (10 connections) — `libs/draxul-kanban/src/kanban_host.cpp`
- **kanban_board.cpp** (9 connections) — `libs/draxul-kanban/src/kanban_board.cpp`
- **reload_board()** (9 connections) — `libs/draxul-kanban/src/kanban_host.cpp`
- **apply_navigation_command()** (8 connections) — `libs/draxul-kanban/src/kanban_host.cpp`
- **selection_has_card()** (7 connections) — `libs/draxul-kanban/src/kanban_board.cpp`
- **selected_card()** (7 connections) — `libs/draxul-kanban/src/kanban_board.cpp`
- **string** (7 connections) — `libs/draxul-kanban/src/kanban_host.cpp`
- **move_selection()** (7 connections) — `libs/draxul-kanban/src/kanban_host.cpp`
- **keep_selection_visible()** (7 connections) — `libs/draxul-kanban/src/kanban_host.cpp`
- **clamp_selection()** (6 connections) — `libs/draxul-kanban/src/kanban_board.cpp`
- **update_status()** (6 connections) — `libs/draxul-kanban/src/kanban_host.cpp`
- **card_kind_for_file()** (5 connections) — `libs/draxul-kanban/src/kanban_board.cpp`
- **sort_columns_for_first_load()** (5 connections) — `libs/draxul-kanban/src/kanban_board.cpp`
- **open_selected_card()** (5 connections) — `libs/draxul-kanban/src/kanban_host.cpp`
- **notify_error()** (5 connections) — `libs/draxul-kanban/src/kanban_host.cpp`
- **icon_for_kind()** (4 connections) — `libs/draxul-kanban/src/kanban_board.cpp`
- **ends_with()** (3 connections) — `libs/draxul-kanban/src/kanban_board.cpp`
- **string_view** (3 connections) — `libs/draxul-kanban/src/kanban_board.cpp`
- **preferred_column_rank()** (3 connections) — `libs/draxul-kanban/src/kanban_board.cpp`
- **KanbanBoard** (3 connections) — `libs/draxul-kanban/src/kanban_board.cpp`
- **KanbanSelection** (3 connections) — `libs/draxul-kanban/src/kanban_board.cpp`
- **CardKind** (2 connections) — `libs/draxul-kanban/src/kanban_board.cpp`
- **string** (2 connections) — `libs/draxul-kanban/src/kanban_board.cpp`
- **init_error()** (2 connections) — `libs/draxul-kanban/src/kanban_host.cpp`
- **status_text()** (2 connections) — `libs/draxul-kanban/src/kanban_host.cpp`
- *... and 2 more nodes in this community*

## Relationships

- [[Kanban Host]] (15 shared connections)
- [[Kanban Store]] (9 shared connections)
- [[Community 289]] (2 shared connections)
- [[Kanban Host 3]] (2 shared connections)
- [[Terminal Host Base]] (2 shared connections)
- [[Kanban Layout]] (2 shared connections)
- [[Vulkan Renderer 4]] (1 shared connections)
- [[Grid Host Base]] (1 shared connections)

## Source Files

- `libs/draxul-kanban/src/kanban_board.cpp`
- `libs/draxul-kanban/src/kanban_host.cpp`

## Audit Trail

- EXTRACTED: 107 (81%)
- INFERRED: 25 (19%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [[index]] to navigate.*