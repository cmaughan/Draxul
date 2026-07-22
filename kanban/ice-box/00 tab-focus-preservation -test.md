# WI 134: Tab Focus Preservation Test

**Type:** test
**Priority:** 00 (highest in this batch)
**Raised by:** Claude (`review-latest.claude.md`)
**Depends on:** None (independent; benefits from WI 125 overlay-registry being cleaner, but not a blocker)

---

## Problem

There are no tests that verify focus is correctly preserved and restored when switching between tabs. If `App::next_tab()` / `App::prev_tab()` lose track of the previously focused pane, or if switching corrupts the input-dispatcher's active host, the user experiences the wrong pane receiving keyboard input after a tab switch — a silent, hard-to-diagnose regression.

The `tab.h` / `tab.cpp` logic carries a `focused_leaf` field but this is exercised only indirectly through full smoke tests that require spawning Neovim.

---

## What to Test

- [ ] Create two tabs, each with a split pane (two leaves each).
- [ ] Focus different leaves in each tab (tab A: leaf 0; tab B: leaf 1).
- [ ] Call `next_tab()` to switch to tab B; verify the input dispatcher's active host is the one focused in tab B.
- [ ] Call `prev_tab()` to return to tab A; verify focus reverts to the correct leaf in tab A.
- [ ] Close one pane in tab B and verify the focused leaf in tab A is not affected.
- [ ] Switch back to tab B and verify its focus has been updated correctly after the close.
- [ ] Verify that `PaneManager::active_host()` returns the expected host for each case without touching `App` god-object state directly.

---

## Implementation Plan

1. **Locate the test file.** Add to `tests/pane_manager_tests.cpp` or create `tests/tab_focus_tests.cpp` if the test scope warrants isolation.

2. **Use `PaneManager::Deps` fake.** `PaneManager` accepts a `Deps` struct — use fake `IWindow`, fake `IGridRenderer`, and a fake `IHost` factory. No Neovim or real renderer needed.

3. **Construct two tabs with split panes.**
   ```cpp
   // Pseudo-code — adapt to real API
   auto mgr = make_pane_manager(fake_deps);
   mgr.open_new_tab();       // tab 0
   mgr.split_vertical();     // two leaves in tab 0
   mgr.set_focus(leaf_id_0a);
   mgr.open_new_tab();       // tab 1
   mgr.split_vertical();
   mgr.set_focus(leaf_id_1b);
   ```

4. **Assert focus state via public API** (`active_host()`, `focused_leaf_id()`) after each `next_tab()` / `prev_tab()` call.

5. **Run under ASan** (`cmake --preset mac-asan`) to catch any use-after-free if a fake host is released during tab switch.

---

## Files Likely Involved

- `app/pane_manager.h` / `app/pane_manager.cpp`
- `app/tab.h` / `app/tab.cpp`
- `tests/pane_manager_tests.cpp` (add test cases) or new `tests/tab_focus_tests.cpp`
- `tests/support/` (check for a suitable fake host fixture)

---

## Acceptance Criteria

- [ ] All new test cases pass under `ctest -R tab_focus` (or the chosen test binary).
- [ ] Tests pass under `mac-asan` preset with no AddressSanitizer or UBSan warnings.
- [ ] No real Neovim process or real renderer is required to run the tests.

---

*Authored by `claude-sonnet-4-6`*
