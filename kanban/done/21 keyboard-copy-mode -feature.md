# Feature: Keyboard-Driven Copy Mode for Terminal Panes

**Type:** feature
**Priority:** 21
**Source:** Gemini review

## Overview

Currently, text selection in terminal panes is mouse-only. Power users (and those in environments where mouse is unavailable) need a keyboard-driven copy mode — similar to tmux's copy mode or Vim's visual mode:

1. Press a keybinding to enter copy mode.
2. Navigate with `h/j/k/l` or arrow keys.
3. Press `v` to start a selection, `V` for line selection.
4. Press `y` to yank (copy to clipboard) and exit copy mode.
5. Press `q` or `Escape` to exit without copying.

## Status

**Completed** 2026-04-07 — implemented in `LocalTerminalHost` (intermediate base for shell-backed hosts; NvimHost intentionally excluded since Neovim has its own visual mode). Copy mode state, key handling, and overlay live alongside `SelectionManager`. `toggle_copy_mode` registered as a GUI action with default `Ctrl+Shift+Space`.

## Implementation plan

### Phase 1: Copy mode state

- [x] Read `libs/draxul-host/include/draxul/host.h` and `selection_manager.cpp`.
- [x] Added `CopyMode { active, selecting, line_mode, cursor, anchor }` struct on `LocalTerminalHost`.

### Phase 2: Enter/exit copy mode

- [x] Added `toggle_copy_mode` GUI action and registered in `gui_actions.h` + `GuiActionHandler`.
- [x] Default keybinding `Ctrl+Shift+Space`.
- [x] Enter seeds the copy-mode cursor from `vt_state().col/row`; exit clears overlay.

### Phase 3: Navigation in copy mode

- [x] `h/j/k/l` and arrow keys move the cursor; `0/HOME/END` jump to line bounds; `g/Shift+G` jump to top/bottom.
- [x] Cursor rendered as a single-cell selection overlay so it is visible without an extra rendering path.

### Phase 4: Selection and yank

- [x] `v` toggles char-mode selection; `V` toggles line-mode.
- [x] `y` yanks the current selection through `SelectionManager::extract_text()` and exits.
- [x] `Escape` / `q` exit without copy.

### Phase 5: Config and keybindings

- [x] `toggle_copy_mode` exposed via `[keybindings]`.
- [x] Documented in `docs/features.md`.

## Acceptance criteria

- [x] Enter/exit via keybinding without crash.
- [x] Cursor moves over the visible grid (scrollback navigation defers to existing scroll path).
- [x] Selection overlay visible while in copy mode.
- [x] Yanked text matches the selected region and lands on the system clipboard.
- [x] Normal input is blocked while copy mode is active (handled in `on_key`).

## Interdependencies

- **Icebox `20 searchable-scrollback -feature`**: both navigate the scrollback; share navigation primitives.
- **`06 scrollback-ring-wrap -test`**: copy mode navigating past the ring boundary must be tested.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
