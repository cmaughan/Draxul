# WI 37 — duplicate-pane

**Type:** feature  
**Priority:** Medium  
**Source:** review-consensus.md §6c — GPT  
**Produced by:** claude-sonnet-4-6

---

## Feature Description

Add a "duplicate pane" action that opens a new pane with the same host kind, working directory, and launch options as the currently focused pane. The new pane should appear adjacent to (i.e., split from) the original pane.

This is a high-frequency workflow for terminal users who want to open a second shell in the same directory as an existing one.

---

## Investigation

- [ ] Read `app/host_manager.cpp` — find `create_host_for_leaf()` and the leaf-split API; understand what parameters drive pane creation.
- [ ] Read `app/app.cpp` or the keybinding dispatch layer — understand how `split_pane_*` actions are triggered today.
- [ ] Read `libs/draxul-host/include/draxul/host.h` — check if `IHost` exposes enough metadata to reconstruct a duplicate (host kind, cwd, launch options).
- [ ] Check the config's `[keybindings]` table for an appropriate action name slot (e.g., `duplicate_pane`).

---

## Implementation Plan

### Step 1: Add host metadata access

- [ ] Ensure `IHost` (or `GridHostBase`) exposes a `launch_descriptor()` method returning enough information to re-create the same host type with the same initial options (host kind enum, working directory, launch args).
- [ ] If this is already exposed (check `NvimHost`, `LocalTerminalHost`), confirm the fields are populated correctly.

### Step 2: Add `duplicate_pane` action

- [ ] In `app/app.cpp` (or wherever `split_pane_horizontal` / `split_pane_vertical` are dispatched), add:
  ```cpp
  case Action::duplicate_pane: {
      auto* focused = host_manager_.focused_host();
      if (focused) {
          auto desc = focused->launch_descriptor();
          host_manager_.split_and_create(desc, SplitDirection::horizontal);
      }
  }
  ```
- [ ] Define the split direction for duplicate as user-configurable or default to horizontal.

### Step 3: Wire keybinding

- [ ] Add `duplicate_pane` to the `[keybindings]` action table in the config layer.
- [ ] Document it in `docs/features.md` under GUI keybindings.
- [ ] Set a sensible default (e.g., no default chord — user must configure it) to avoid conflicts.

### Step 4: NvimHost cwd capture

- [ ] For `NvimHost`, the working directory is not directly accessible (Neovim manages it). Query via `nvim_call_function("getcwd", [])` on creation and cache it.
- [ ] For `LocalTerminalHost`, the cwd is typically tracked already via OSC 7 or from the initial launch options.

### Step 5: Tests

- [ ] Add a test: create a host, trigger duplicate, assert a second host of the same type is created.
- [ ] Test that duplicating a host without a tracked cwd falls back to the app's initial cwd (not empty or crash).

---

## Acceptance Criteria

- [ ] `duplicate_pane` action creates a new pane of the same host type adjacent to the current pane.
- [ ] The new pane opens in the same working directory as the source pane (or a reasonable fallback).
- [ ] The action is listed in `docs/features.md`.
- [ ] Keybinding is documented and can be configured in `config.toml`.
- [ ] Smoke test passes.

---

## Dependencies

- [ ] WI 14 (hostmanager-split-close-stress) should be passing first — confirms the split infrastructure is stable.
- [ ] Touches the same pane-creation path as WI 39 (right-click context menus) — coordinate so the action is accessible from both keybinding and context menu.
