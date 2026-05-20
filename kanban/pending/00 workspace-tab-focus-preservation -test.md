# WI 134: Workspace Tab Focus Preservation Test

**Type:** test
**Priority:** 00 (highest in this batch)
**Raised by:** Claude (`review-latest.claude.md`)
**Depends on:** None (independent; benefits from WI 125 overlay-registry being cleaner, but not a blocker)

---

## Problem

There are no tests that verify focus is correctly preserved and restored when switching between workspace tabs. If `App::next_workspace()` / `App::prev_workspace()` lose track of the previously focused pane, or if switching corrupts the input-dispatcher's active host, the user experiences the wrong pane receiving keyboard input after a tab switch — a silent, hard-to-diagnose regression.

The `workspace.h` / `workspace.cpp` logic carries a `focused_leaf` field but this is exercised only indirectly through full smoke tests that require spawning Neovim.

---

## What to Test

- [ ] Create two workspaces, each with a split pane (two leaves each).
- [ ] Focus different leaves in each workspace (workspace A: leaf 0; workspace B: leaf 1).
- [ ] Call `next_workspace()` to switch to workspace B; verify the input dispatcher's active host is the one focused in workspace B.
- [ ] Call `prev_workspace()` to return to workspace A; verify focus reverts to the correct leaf in workspace A.
- [ ] Close one pane in workspace B and verify the focused leaf in workspace A is not affected.
- [ ] Switch back to workspace B and verify its focus has been updated correctly after the close.
- [ ] Verify that `HostManager::active_host()` returns the expected host for each case without touching `App` god-object state directly.

---

## Implementation Plan

1. **Locate the test file.** Add to `tests/host_manager_tests.cpp` or create `tests/workspace_focus_tests.cpp` if the test scope warrants isolation.

2. **Use `HostManager::Deps` fake.** `HostManager` accepts a `Deps` struct — use fake `IWindow`, fake `IGridRenderer`, and a fake `IHost` factory. No Neovim or real renderer needed.

3. **Construct two workspaces with split panes.**
   ```cpp
   // Pseudo-code — adapt to real API
   auto mgr = make_host_manager(fake_deps);
   mgr.open_new_workspace();       // workspace 0
   mgr.split_vertical();           // two leaves in ws 0
   mgr.set_focus(leaf_id_0a);
   mgr.open_new_workspace();       // workspace 1
   mgr.split_vertical();
   mgr.set_focus(leaf_id_1b);
   ```

4. **Assert focus state via public API** (`active_host()`, `focused_leaf_id()`) after each `next_workspace()` / `prev_workspace()` call.

5. **Run under ASan** (`cmake --preset mac-asan`) to catch any use-after-free if a fake host is released during workspace switch.

---

## Files Likely Involved

- `app/host_manager.h` / `app/host_manager.cpp`
- `app/workspace.h` / `app/workspace.cpp`
- `tests/host_manager_tests.cpp` (add test cases) or new `tests/workspace_focus_tests.cpp`
- `tests/support/` (check for a suitable fake host fixture)

---

## Acceptance Criteria

- [ ] All new test cases pass under `ctest -R workspace_focus` (or the chosen test binary).
- [ ] Tests pass under `mac-asan` preset with no AddressSanitizer or UBSan warnings.
- [ ] No real Neovim process or real renderer is required to run the tests.

---

*Authored by `claude-sonnet-4-6`*
