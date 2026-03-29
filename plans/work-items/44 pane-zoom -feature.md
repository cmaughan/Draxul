# 44 pane-zoom -feature

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.claude.md [C]; review-latest.gemini.md [G]*

## Problem

When working in a split-pane layout, users frequently want to temporarily expand the focused
pane to fill the entire window (e.g., to read a long log, or to type a long command).  There
is currently no way to do this without manually closing the other panes.

This is a standard feature in tmux (`Ctrl+B z`), kitty, WezTerm, and other terminal
multiplexers.  It is trivially reversible and high daily-use value.

## Acceptance Criteria

- [ ] A new GUI action `zoom_pane` (or `toggle_zoom`) is registered in `GuiActionHandler`.
- [ ] A default keybinding is defined (suggested: `Ctrl+B, z` for tmux muscle-memory users,
      or a configurable binding in `[keybindings]`).
- [ ] When `zoom_pane` is triggered, the focused pane expands to fill the full window layout
      area.  Other panes are hidden but their hosts remain running.
- [ ] Triggering `zoom_pane` again (or any pane navigation action) restores the previous
      split layout.
- [ ] The `SplitTree` layout is preserved exactly during the zoom — ratios and pane order are
      restored on un-zoom.
- [ ] The renderer receives the correct full-window size for the zoomed pane and correctly
      resizes back on un-zoom.
- [ ] Works correctly with 2-pane, 3-pane, and deep-split layouts.

## Implementation Plan

1. Read `app/split_tree.h/cpp` and `app/host_manager.h/cpp` to understand how pane layout
   and rendering are driven.
2. Add a `bool zoom_state_` flag and a `std::optional<SplitTree> pre_zoom_tree_` snapshot to
   `HostManager` (or wherever layout state lives).
3. Implement `toggle_zoom()`:
   - If not zoomed: snapshot the current tree, replace it with a single-leaf tree containing
     the focused host, resize the focused host to full window.
   - If zoomed: restore the snapshot tree, resize all hosts to their pre-zoom dimensions.
4. Register `zoom_pane` action in `GuiActionHandler`.
5. Add a default keybinding.
6. Update `docs/features.md`.
7. Write a test: create a 2-pane layout, call toggle_zoom, verify the focused pane is
   full-window, call again, verify the original layout is restored.

## Files Likely Touched

- `app/host_manager.h` / `app/host_manager.cpp`
- `app/gui_action_handler.cpp`
- `app/input_dispatcher.cpp` (keybinding registration)
- `docs/features.md`
- `tests/host_manager_zoom_tests.cpp` (new, or added to existing host_manager_tests)

## Interdependencies

- Independent of other open WIs at the code level.
- **WI 45** (`pane-management-actions`) is adjacent — do these in sequence to avoid
  `HostManager` conflicts.
