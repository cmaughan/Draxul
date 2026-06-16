# Nvim Host

> 42 nodes

## Key Concepts

- **nvim_host.cpp** (31 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **initialize_host()** (8 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **wire_ui_callbacks()** (8 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **refresh_cursor_style()** (7 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **on_flush()** (6 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **apply_ui_option()** (5 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **on_viewport_changed()** (5 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **on_font_metrics_changed_impl()** (5 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **handle_rpc_request()** (5 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **mark_activity()** (4 connections) — `libs/draxul-host/src/grid_host_base.cpp`
- **string** (4 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **is_running()** (4 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **pump()** (4 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **queue_resize_request()** (4 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **on_busy()** (4 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **handle_clipboard_set()** (4 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **MpackValue** (3 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **attach_ui()** (3 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **current_blink_timing()** (3 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **status_text()** (2 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **init_error()** (2 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **shutdown()** (2 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **on_key()** (2 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **on_text_input()** (2 connections) — `libs/draxul-host/src/nvim_host.cpp`
- **on_text_editing()** (2 connections) — `libs/draxul-host/src/nvim_host.cpp`
- *... and 17 more nodes in this community*

## Relationships

- [[Grid Host Base]] (9 shared connections)
- [[Terminal Host Base Csi]] (4 shared connections)
- [[Terminal Host Base]] (2 shared connections)
- [[Host RPC]] (1 shared connections)

## Source Files

- `libs/draxul-host/src/grid_host_base.cpp`
- `libs/draxul-host/src/nvim_host.cpp`

## Audit Trail

- EXTRACTED: 135 (88%)
- INFERRED: 18 (12%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [[index]] to navigate.*