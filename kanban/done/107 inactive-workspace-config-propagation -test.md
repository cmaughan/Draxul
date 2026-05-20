# WI 107 — inactive-workspace-config-propagation

**Type:** test
**Priority:** 4 (test — acceptance criterion for WI 104)
**Source:** review-consensus.md §2 [GPT]
**Produced by:** claude-sonnet-4-6

---

## Problem / Gap

No test verifies that font metrics or config changes reach hosts in **inactive** workspaces. WI 104 fixes the propagation; this test locks in the fix.

---

## What to Test

1. **Font metrics propagation:** Create an `App` with two workspaces (or a fake multi-host manager). Switch to workspace 2. Change the font size. Assert that hosts in workspace 1 (inactive) receive `apply_font_metrics()` or equivalent update.
2. **Config reload propagation:** Reload config with a changed `enable_ligatures` value. Assert that *all* panes across all workspaces see the new setting — not only the currently visible workspace.
3. **No double-apply:** Verify that hosts in the *active* workspace are not updated twice (once as active, once as inactive).

---

## Implementation

- [x] Added test to `tests/app_smoke_tests.cpp` (the App-level smoke harness already
  exposes FakeWindow + FakeRenderer wiring, so this is the natural seam).
- [x] Test creates an App with `new_tab=Ctrl+T` and `reload_config=Ctrl+Alt+R` keybindings,
  spawns a second workspace via the user-facing `new_tab` action, then rewrites
  `config.toml` and triggers reload via Ctrl+Alt+R.
- [x] A namespace-level `g_all_reload_hosts` tracker captures every host the factory ever
  produced (including hosts in inactive workspaces).
- [x] Asserts each tracked host saw exactly one `reload_count` bump (no double-apply, no
  skip) and that `font_metrics_changed_count >= 1` after the font_size change.
- [x] Build: `cmake --build build --target draxul-tests`
- [x] Run: `ctest --test-dir build -R draxul-tests` — passes (13 assertions in the new case).

---

## Acceptance Criteria

- [x] Test passes after WI 104 fix is applied.
- [x] Test **fails** if you revert WI 104 (i.e., the test is a genuine regression guard
      — without the workspaces_ fan-out, host[0] in the inactive workspace would have
      `reload_count == 0`).
- [x] No false positives — runs cleanly under the current tree.

---

## Interdependencies

- **WI 104** (config-font-inactive-workspace-bias -bug) — fix first, then write this test to lock it in.
- **WI 66** (config-reload-multi-pane -test, existing) — related; check for overlap and avoid duplicate assertions.
