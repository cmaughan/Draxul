# Terminal Host Base Csi

> 25 nodes

## Key Concepts

- **grid_cols** (33 connections) — `libs/draxul-grid/src/grid.cpp`
- **terminal_host_base_csi.cpp** (20 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **handle_csi()** (12 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **vector** (11 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **csi_erase()** (8 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **on_line_scrolled_off()** (7 connections) — `libs/draxul-host/src/local_terminal_host.cpp`
- **param_or()** (7 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **string_view** (7 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **erase_display()** (6 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **csi_scroll()** (6 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **csi_insert_delete()** (6 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **clear_cell()** (5 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **newline()** (5 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **erase_line()** (5 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **csi_cursor_move()** (5 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **csi_margins()** (5 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **extract_osc7_path()** (5 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **handle_osc()** (5 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **csi_sgr()** (4 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **csi_dsr()** (4 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **percent_decode()** (4 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **string** (4 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **handle_control()** (3 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **handle_esc()** (3 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **csi_da()** (3 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`

## Relationships

- [[Terminal Host Base]] (15 shared connections)
- [[Kanban Host]] (6 shared connections)
- [[Local Terminal Host]] (6 shared connections)
- [[Nvim Host]] (4 shared connections)
- [[Shell Host Win]] (2 shared connections)
- [[Grid Grid]] (1 shared connections)
- [[Shell Host Win 2]] (1 shared connections)
- [[Grid Host Base]] (1 shared connections)
- [[Terminal Sgr]] (1 shared connections)

## Source Files

- `libs/draxul-grid/src/grid.cpp`
- `libs/draxul-host/src/local_terminal_host.cpp`
- `libs/draxul-host/src/terminal_host_base.cpp`
- `libs/draxul-host/src/terminal_host_base_csi.cpp`

## Audit Trail

- EXTRACTED: 119 (65%)
- INFERRED: 64 (35%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [[index]] to navigate.*