# 00 keybinding-removal-broken -bug

**Priority:** CRITICAL
**Type:** Bug (documented behaviour does not match implementation)
**Raised by:** GPT-4o (primary), confirmed by Claude
**Model:** claude-sonnet-4-6

---

## Problem

`FEATURES.md:73` documents that setting `copy = ""` in `[keybindings]` removes the default binding. In practice this is silently ignored: `app_config_io.cpp:233` only overwrites a binding entry when `parse_gui_keybinding()` succeeds, and an empty string at `keybinding_parser.cpp:183` returns `std::nullopt` (parse failure). The net result is that users cannot disable a default binding despite documentation promising they can.

---

## Code Locations

- `libs/draxul-app-support/src/app_config_io.cpp:233` — where the keybinding is (not) replaced
- `libs/draxul-app-support/src/keybinding_parser.cpp:183` — where `""` returns `nullopt`
- `FEATURES.md:73` — the documented contract

---

## Implementation Plan

- [x] Read `app_config_io.cpp` around line 233 and `keybinding_parser.cpp` around line 183 to understand the exact parse path.
- [x] Determine the in-memory representation: where are default bindings stored before user config is applied? (likely `AppConfig::keybindings` vector built from defaults.)
- [x] Decide on the removal sentinel: either treat an empty string specially before calling `parse_gui_keybinding()`, or add a `remove_binding()` path in `AppConfig` that erases any entry matching the action name.
- [x] When `config.toml` contains `action = ""`, remove the matching default from the keybindings table (do not silently skip).
- [x] Emit a log entry at `debug` level: `"Keybinding 'action' removed by user config."` so it is visible in `--log-level debug` output.
- [x] Update `FEATURES.md` if the documented format changes (it shouldn't).
- [x] Build: `cmake --build build --target draxul draxul-tests && py do.py smoke`
- [x] Run `clang-format` on all modified files.

---

## Acceptance Criteria

- `copy = ""` in `[keybindings]` disables the default copy binding (Ctrl+C / Cmd+C no longer triggers `copy`).
- Other bindings in the table are unaffected.
- A binding that was never assigned a default is silently ignored (no error on `nonexistent = ""`).
- The regression test `06 keybinding-removal-regression -test` passes after this fix.

---

## Interdependencies

- **`06 keybinding-removal-regression -test`** — write the test before or alongside this fix to prove it works.
- No upstream blockers.

---

*claude-sonnet-4-6*
