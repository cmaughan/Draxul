# Selection Clear Repaint Bug

**Type:** bug
**Priority:** 01
**Raised by:** Claude

## Summary

`SelectionManager::clear()` removes overlay cells but never calls `request_frame()`, so the stale selection highlight stays visible until an unrelated repaint fires.

## Background

After a copy action (`dispatch_action("copy")`), `selection_.clear()` is called which pushes an empty overlay via `set_overlay_cells({})`. However, the renderer only repaints on the next frame request. Without an explicit `request_frame()` call in `clear()`, the selection highlight persists on screen until something else triggers a repaint (e.g., cursor blink, nvim output).

The `request_frame` callback already existed in `SelectionManager::Callbacks` and was already wired from `TerminalHostBase`. It was simply not called from `clear()`.

## Implementation Plan

### Files to modify
- `libs/draxul-host/src/selection_manager.cpp` — call `request_frame()` in `clear()` when overlay cells were actually active

### Steps
- [x] Read `selection_manager.h` and `selection_manager.cpp` fully
- [x] Confirm `request_frame` callback already exists in `Callbacks` struct and is wired in `TerminalHostBase`
- [x] In `clear()`, save `sel_active_` before clearing, then call `cbs_.request_frame()` only when `was_active` is true
- [x] Build: `cmake --build build --target draxul draxul-tests --parallel`
- [x] Run: `./build/tests/draxul-tests` — all tests pass
- [x] Smoke: `./build/draxul --smoke-test` — passes
- [x] Format: clang-format on touched file

## Depends On
- None

## Blocks
- None

## Notes
The guard (`was_active`) ensures `request_frame()` is only called when there were actual overlay cells to remove. Calling it unconditionally would cause spurious repaints on every mouse-down (which calls `begin_drag` → `clear()`).

> Work item produced by: claude-sonnet-4-6
