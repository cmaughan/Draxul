# 45 pane-management-actions -feature

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.gpt.md [P]; review-latest.gemini.md [G]*

## Status

**Completed** — `close_pane`, `restart_host`, and `swap_pane` actions are wired up, registered with `GuiActionHandler`, default-bound, and documented in `docs/features.md`. `duplicate_pane` and `split_same_host` were explicitly deferred out of scope below; track them as separate items if revisited.

## Problem

Pane creation is first-class in Draxul (split horizontal, split vertical) but pane lifecycle
management is not:

- There is no `close_pane` action — the user must close the host (e.g., exit the shell)
  from inside the pane, which is not always possible (e.g., a frozen nvim).
- There is no `duplicate_pane` action — opening a second pane with the same host type, CWD,
  and launch args requires going through the split flow manually.
- There is no `restart_host` action — a crashed or frozen host cannot be restarted without
  closing and re-splitting the pane.
- When splitting, new panes always use the platform default shell, not the host type of the
  focused pane.  A user working in a nvim pane who splits gets a shell, not a second nvim.

## Acceptance Criteria

- [x] `close_pane` action: closes the focused pane and its host, removes it from the split
      tree.  If it is the last pane, exits the application.
- [ ] `duplicate_pane` action: creates a new pane (adjacent to the focused one, using the
      current split direction) with the same host kind, CWD, and launch args as the focused
      pane. (Deferred — not in scope for this round.)
- [x] `restart_host` action: kills the current host process in the focused pane and restarts
      it with the same arguments.  The grid is cleared and reused.
- [ ] `split_same_host` action (or a config option `split_inherits_host = true`): when
      splitting, new pane uses the host type of the focused pane instead of the default shell.
      (Deferred — not in scope for this round.)
- [x] `swap_pane` action: swaps the focused pane with the next pane in spatial order.
- [x] All new actions have default keybindings and are documented in `docs/features.md`.
- [x] All new actions are exposed in the GUI action handler so they can be bound to
      keybindings in `config.toml`.

## Implementation Plan

1. Read `app/host_manager.cpp` and `app/split_tree.cpp` for the pane lifecycle API.
2. Read `app/gui_action_handler.cpp` and `app/input_dispatcher.cpp` for how existing actions
   are registered and dispatched.
3. Implement `close_pane`:
   - Call `host->shutdown()` on the focused host.
   - Remove the leaf from `SplitTree`.
   - If tree is now empty, exit application.
4. Implement `duplicate_pane`:
   - Read the focused host's kind, CWD (query from the process or store at launch), and args.
   - Create a new host with those parameters.
   - Insert a new leaf adjacent to the focused leaf in `SplitTree`.
5. Implement `restart_host`:
   - Call `host->shutdown()`.
   - Clear the grid for this pane.
   - Re-initialize the same host type with the same args.
6. Implement `split_same_host` logic (or config option).
7. Register all actions in `GuiActionHandler`.
8. Update `docs/features.md`.
9. Write tests for close, duplicate, and restart using fake host infrastructure.

## Files Likely Touched

- `app/host_manager.h` / `app/host_manager.cpp`
- `app/gui_action_handler.cpp`
- `app/input_dispatcher.cpp`
- `app/split_tree.h` / `app/split_tree.cpp` (close_pane leaf removal)
- `docs/features.md`
- `tests/pane_lifecycle_tests.cpp` (new)

## Interdependencies

- **WI 44** (`pane-zoom`) is adjacent — do in sequence, not in parallel.
- `close_pane` coordinate with icebox `native-tab-bar` — they share the leaf-removal code path.
- `duplicate_pane` requires knowing the focused pane's CWD; verify this is accessible from
  the host API or add a `cwd()` accessor.
