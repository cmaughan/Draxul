# Terminal Host Base

> 51 nodes

## Key Concepts

- **terminal_host_base.cpp** (38 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **log_would_emit()** (37 connections) — `libs/draxul-types/src/log.cpp`
- **log_printf()** (37 connections) — `libs/draxul-types/src/log.cpp`
- **set_cursor_display_override()** (13 connections) — `libs/draxul-host/src/grid_host_base.cpp`
- **pump()** (13 connections) — `libs/draxul-host/src/local_terminal_host.cpp`
- **csi_mode()** (11 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **advance_cursor_blink()** (10 connections) — `libs/draxul-host/src/grid_host_base.cpp`
- **pump()** (10 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **end_output_cursor_batch()** (9 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **consume_output()** (9 connections) — `libs/draxul-host/src/terminal_host_base_csi.cpp`
- **end_synchronized_output()** (7 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **begin_output_cursor_batch()** (7 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **string** (6 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **string_view** (6 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **on_key()** (6 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **trace_cursor_presentation_state()** (6 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **reconcile_provisional_cursor_after_pump()** (6 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **write_cluster()** (6 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **on_mouse_move()** (5 connections) — `libs/draxul-host/src/local_terminal_host.cpp`
- **describe_text_for_log()** (5 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **on_text_input()** (5 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **maybe_capture_pty_chunk()** (5 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **begin_synchronized_output()** (5 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **compact_attr_ids()** (5 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- **set_grid_cell_for_alt_screen()** (4 connections) — `libs/draxul-host/src/terminal_host_base.cpp`
- *... and 26 more nodes in this community*

## Relationships

- [[Grid Host Base]] (28 shared connections)
- [[Terminal Host Base Csi]] (15 shared connections)
- [[Local Terminal Host]] (14 shared connections)
- [[Kanban Host]] (9 shared connections)
- [[Types Log]] (8 shared connections)
- [[Renderer State]] (6 shared connections)
- [[Shell Host Win]] (4 shared connections)
- [[SDL Window]] (3 shared connections)
- [[Nvim Host]] (2 shared connections)
- [[Kanban Host 3]] (2 shared connections)
- [[Kanban Board]] (2 shared connections)
- [[Vulkan Renderer 8]] (2 shared connections)

## Source Files

- `libs/draxul-host/src/grid_host_base.cpp`
- `libs/draxul-host/src/local_terminal_host.cpp`
- `libs/draxul-host/src/terminal_host_base.cpp`
- `libs/draxul-host/src/terminal_host_base_csi.cpp`
- `libs/draxul-types/src/log.cpp`

## Audit Trail

- EXTRACTED: 176 (53%)
- INFERRED: 154 (47%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [[index]] to navigate.*