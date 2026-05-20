# App Keybinding Dispatch Test

**Type:** test
**Priority:** 19
**Raised by:** Claude

## Summary

Add unit tests for the GUI keybinding dispatch logic in `App`, covering: correct action triggered for each configured keybinding, overlapping bindings resolved deterministically, unknown action names handled gracefully without crash, and config-defined bindings parsed correctly from `config.toml`.

## Background

The keybinding system is configured via `[keybindings]` in `config.toml` and maps key combinations to named actions (`toggle_debug_panel`, `copy`, `paste`, `font_increase`, `font_decrease`, `font_reset`). Bugs in this dispatch layer (wrong action fired, unknown action crashes the process, overlapping bindings causing undefined behaviour) directly affect daily usability. Tests here provide a safety net for config parsing and dispatch logic.

## Implementation Plan

### Files to modify
- `tests/keybinding_dispatch_tests.cpp` — created

### Steps
- [x] Write test: parse a `config.toml` snippet binding `Ctrl+C` to `copy`; synthesise a `KeyEvent` for Ctrl+C; verify `copy` action is dispatched
- [x] Write test: bind the same key to two different actions; verify the first binding wins (or document the resolution policy and test for it)
- [x] Write test: config contains an unknown action name `"launch_rockets"`; verify no crash, warning logged
- [x] Write test: `font_increase` and `font_decrease` keybindings fire the correct font-size adjustment
- [x] Write test: `toggle_debug_panel` binding fires the debug panel toggle
- [x] Write test: no bindings configured (empty `[keybindings]` table); all actions unreachable via keybindings; no crash on any key event
- [x] Write test: modifier-only key events (just Ctrl pressed, no secondary key) do not trigger any binding
- [x] Register with ctest (added to draxul-tests in tests/CMakeLists.txt)

## Depends On
- None

## Blocks
- None

## Notes
The test harness will need access to the keybinding parser and dispatcher, which may be in `app/app.cpp` or `libs/draxul-app-support/`. If the logic is not yet extracted into a testable unit (see work item `31`), write the tests to at minimum exercise the config parsing in isolation.

> Work item produced by: claude-sonnet-4-6
