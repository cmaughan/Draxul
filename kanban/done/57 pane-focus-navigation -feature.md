# WI 57 — pane-focus-navigation

**Type**: feature  
**Priority**: 9 (highest-ranked new QoL feature by two agents)  
**Source**: review-consensus.md §F1 [P][G]  
**Produced by**: claude-sonnet-4-6

---

## Goal

Add keyboard-driven pane focus navigation: `focus_left`, `focus_right`, `focus_up`, `focus_down` GUI actions with default bindings. Currently the only way to move focus between split panes is mouse click.

---

## Background

The split pane tree is managed by `HostManager` and/or the split-tree code. Focus is tracked per-pane. The `InputDispatcher` routes events to the currently-focused host. The GUI action system (`GuiActionHandler`) dispatches named actions to `App`.

---

## Tasks

- [x] Read `app/host_manager.cpp` and `app/host_manager.h` — understand how focus is tracked and how the split tree is navigated. Identify the data structure for the pane tree.
- [x] Read `app/gui_action_handler.cpp` — understand how to register a new GUI action.
- [x] Read `app/app.cpp` — understand how existing pane actions (e.g., split, close) are implemented; use this as the model for focus navigation.
- [x] Design the focus navigation algorithm: given the focused pane and a direction (left/right/up/down), find the adjacent pane in the split tree. For a binary split tree, "left/right" corresponds to horizontal splits and "up/down" to vertical splits; navigate to the sibling subtree's deepest same-side leaf.
- [x] Implement `focus_left`, `focus_right`, `focus_up`, `focus_down` in `HostManager` (or wherever pane focus is set).
- [x] Register the four actions in `GuiActionHandler::action_map()`.
- [x] Add entries to `kKnownGuiActions` in `app_config_io.cpp` (avoid repeating the WI 51 bug).
- [x] Add default keybindings: `Ctrl+H/J/K/L` vim-style (Ctrl+H=focus_left, Ctrl+J=focus_down, Ctrl+K=focus_up, Ctrl+L=focus_right).
- [x] Update `docs/features.md` — add the new actions to the default keybindings table and the Split Panes section.
- [x] Build and run smoke test: `cmake --build build --target draxul draxul-tests && py do.py smoke`

---

## Acceptance Criteria

- With two or more panes open, the four directional actions move focus to the adjacent pane (or are no-ops if there is no adjacent pane in that direction).
- Default bindings work and do not conflict with existing bindings.
- Actions are persisted correctly in `config.toml` (no recurrence of WI 51).
- `docs/features.md` is updated.
- Smoke test passes.

---

## Interdependencies

- **WI 45** (pane-management-actions) also touches `HostManager`; sequence these or coordinate if working in parallel.
- **WI 51** (gui-action-config-drift) — WI 51 should be merged first so the `kKnownGuiActions` discipline is established before adding new actions here.

---

## Notes for Agent

- A sub-agent is appropriate here given the need to read multiple large files before writing. Brief it with the function names in this document.
- Start with just two directions (left/right) and get them working before adding up/down, as the tree traversal algorithm may vary by split orientation.
- If the split tree is a binary tree, the "no adjacent pane in direction X" case at the root should be a silent no-op.
