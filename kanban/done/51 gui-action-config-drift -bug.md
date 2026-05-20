# WI 51 — gui-action-config-drift

**Type**: bug  
**Priority**: 3 (silent data loss on config save)  
**Source**: review-consensus.md §B4 [P]  
**Produced by**: claude-sonnet-4-6

---

## Problem

Runtime GUI actions and the serialisation list are maintained in two separate places:

- **Runtime**: `GuiActionHandler::action_map()` — `app/gui_action_handler.cpp:21`
- **Serialisation**: `kKnownGuiActions` — `libs/draxul-config/src/app_config_io.cpp:33`

`toggle_megacity_ui` and `edit_config` exist in the runtime map but are absent from `kKnownGuiActions`. `AppConfig::serialize()` uses `kKnownGuiActions` to decide which keybindings to persist. Any user who binds or unbinds either action will lose that customisation on the next config save because `serialize()` silently omits unknown actions.

---

## Tasks

- [x] Read `app/gui_action_handler.cpp` — list every action name in `action_map()`.
- [x] Read `libs/draxul-config/src/app_config_io.cpp` — find `kKnownGuiActions` and compare it against the runtime list.
- [x] Add every action that is in the runtime map but missing from `kKnownGuiActions` (at minimum: `toggle_megacity_ui`, `edit_config`; audit for any others).
- [x] Check whether any action in `kKnownGuiActions` no longer exists in the runtime map (stale entries can be left or removed — note the decision).
- [x] Verify `AppConfig::serialize()` / round-trip: write a config with a custom `toggle_megacity_ui` binding, save, reload, and confirm it survives.
- [x] Build and run: `cmake --build build --target draxul draxul-tests && py do.py smoke`

---

## Acceptance Criteria

- `kKnownGuiActions` is a superset of all names returned by `GuiActionHandler::action_map()`.
- Binding/unbinding `toggle_megacity_ui` and `edit_config` round-trips cleanly through `AppConfig::serialize()` / load.
- No new test failures.

---

## Interdependencies

- None; this is a small data-only change in `app_config_io.cpp`.
- The related test work item for config round-trip (`04 gui-action-config-persistence`, complete) can serve as a guide for writing a quick manual test.

---

## Notes for Agent

- Do not restructure `kKnownGuiActions` into a different data structure; just add the missing entries.
- If a future agent wants to unify the two lists, that belongs in a separate refactor work item.
- Completed by extending the config/parser action lists to cover the runtime actions used by `GuiActionHandler`, including `toggle_megacity_ui`, `command_palette`, `edit_config`, and `reload_config`.
- Verified with round-trip coverage in `tests/app_config_tests.cpp`, plus targeted config/app smoke coverage.
