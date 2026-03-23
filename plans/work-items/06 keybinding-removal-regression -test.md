# 06 keybinding-removal-regression -test

**Priority:** HIGH
**Type:** Test (regression guard for keybinding removal fix)
**Raised by:** GPT-4o
**Model:** claude-sonnet-4-6

---

## Problem

There is no test that verifies `copy = ""` in `[keybindings]` removes the default copy binding. Before the fix in `00 keybinding-removal-broken -bug`, this would pass silently while the binding remained active. After the fix this test is the guard against regression.

---

## Code Locations

- `tests/app_config_tests.cpp` — existing keybinding config tests live here
- `libs/draxul-app-support/src/app_config_io.cpp` — the parse path under test
- `libs/draxul-app-support/src/keybinding_parser.cpp` — the empty-string path

---

## Implementation Plan

- [ ] Read `tests/app_config_tests.cpp` to understand the existing test patterns for keybinding config.
- [ ] Read `app_config_io.cpp` to understand how keybindings are parsed from TOML and merged with defaults.
- [ ] Add a test case: construct a TOML string with `[keybindings]\ncopy = ""`. Parse it via the same path as production. Assert the resulting `AppConfig::keybindings` does not contain an entry for the `copy` action.
- [ ] Add a second test case: verify that a binding set to `""` for an action that has no default is silently ignored (no error, no crash).
- [ ] Add a third test case: verify that setting one binding to `""` does not affect other bindings in the same table (isolation).
- [ ] Build test target: `cmake --build build --target draxul-tests`
- [ ] Run: `ctest --test-dir build -R draxul-tests`
- [ ] Run `clang-format` on modified test files.

---

## Acceptance Criteria

- Test fails before `00 keybinding-removal-broken -bug` is applied.
- Test passes after the fix.
- Existing keybinding tests are unaffected.

---

## Interdependencies

- **`00 keybinding-removal-broken -bug`** — implement fix first (or write test first to see it fail, then fix).
- No other blockers.

---

*claude-sonnet-4-6*
