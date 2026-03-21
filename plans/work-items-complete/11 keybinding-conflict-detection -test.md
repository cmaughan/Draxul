# 11 keybinding-conflict-detection -test

**Priority:** MEDIUM
**Type:** Test
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

If a user configures two actions to the same key in `config.toml`, the current behaviour is undefined: silent last-wins, or potentially both actions firing. There is no test for this case, and no documented contract about what should happen.

---

## Implementation Plan

- [x] Read `libs/draxul-app-support/include/draxul/app_config.h` and the keybinding parsing code.
- [x] Read `libs/draxul-app-support/src/` for keybinding load/apply logic.
- [x] Decide on the correct conflict policy (last-wins with WARN is simplest and user-friendly):
  - Policy is already implemented: WARN + first-wins at dispatch time (the `config_from_toml` duplicate detection loop at lines 320-333 of `app_config.cpp` logs a WARN; dispatch iterates in order and returns the first match).
  - Just write the tests.
- [x] Write `tests/keybinding_conflict_tests.cpp`:
  - Construct an `AppConfig` with two bindings for the same key (e.g., both `toggle_diagnostics` and `copy` bound to `Ctrl+D`).
  - Load the config.
  - Assert: WARN is logged about the conflict.
  - Assert: the first binding wins deterministically (no crash, no silent double-action).
  - Assert: Non-conflicting bindings still work correctly.
- [x] Add to `tests/CMakeLists.txt`.
- [x] Run ctest.

---

## Acceptance

- Duplicate keybinding in config: clear WARN logged, deterministic resolution (no crash, no silent failure).
- Normal (non-conflicting) keybindings continue to work.

---

## Interdependencies

- No upstream dependencies.
- Relates to the `dispatch-gui-action-map` refactor (complete) — the map-based dispatch should make conflict detection straightforward.
