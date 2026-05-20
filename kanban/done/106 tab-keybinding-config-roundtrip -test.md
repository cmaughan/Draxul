# WI 106 — tab-keybinding-config-roundtrip

**Type:** test
**Priority:** 4 (test — acceptance criterion for WI 102)
**Source:** review-consensus.md §2 [GPT]
**Produced by:** claude-sonnet-4-6

---

## Problem / Gap

There is no test verifying that the tab-management action names (`new_tab`, `close_tab`, `next_tab`, `prev_tab`, `activate_tab:1`…`activate_tab:9`) survive a serialize → deserialize round-trip through `config.toml`. WI 102 fixes the allowlist; this test locks in that fix and prevents regressions.

---

## What to Test

1. **Serialization round-trip:** Build an `AppConfig` with a custom keybinding for each of `new_tab`, `close_tab`, `next_tab`, `prev_tab`, and `activate_tab:3`. Serialize to TOML string. Deserialize back. Verify the custom bindings are present and unchanged.
2. **Parser recognises the names:** Feed a TOML snippet containing `new_tab = "Ctrl+T"` to the keybinding parser and verify it is parsed without a warning or unknown-key error.
3. **Registry parity check:** Enumerate all action names known to `GuiActionHandler` / the action registry and assert that each one is also present in the config parser allowlist. This prevents future tab-style omissions from going unnoticed.

---

## Implementation

- [x] Added `tests/tab_keybinding_roundtrip_tests.cpp` with 3 test cases under the
  `[config][keybinding][tab]` / `[parity]` tags.
- [x] Used the in-memory `AppConfig::serialize()` / `AppConfig::parse()` round-trip — no
  filesystem touched.
- [x] Registry parity test enumerates `GuiActionHandler::action_names()` and asserts every
  runtime name is accepted by `is_known_gui_action_config_key()` (with `:1`..`:9` expansion
  for tab-indexed actions).
- [x] Build: `cmake --build build --target draxul-tests`
- [x] Run: passes under the `[config]` filter.

---

## Acceptance Criteria

- [x] Test added; all assertions pass.
- [x] `new_tab`, `close_tab`, `next_tab`, `prev_tab`, and `activate_tab:3` survive a full round-trip.
- [x] Registry-parity assertion ensures every runtime action is accepted by the parser.

---

## Interdependencies

- **WI 102** (tab-keybinding-config-allowlist -bug) — this test is the acceptance criterion; write after or alongside the fix.
- **WI 67** (gui-action-registry-parity -test, existing) — related; the registry parity check here is a narrower version of WI 67. Co-ordinate to avoid duplication.
