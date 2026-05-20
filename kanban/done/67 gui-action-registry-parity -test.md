---
# WI 67 — GUI Action Parity Test Across All Three Registries

**Type:** test  
**Priority:** high (prerequisite for WI 71; catches silent config data loss)  
**Raised by:** [P] GPT (HIGH), [C] Claude  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

GUI action names appear in three independent locations:
1. `libs/draxul-config/src/app_config_io.cpp` — config parsing
2. `libs/draxul-config/src/keybinding_parser.cpp` — keybinding parsing  
3. `app/gui_action_handler.cpp` — runtime dispatch table

If an action exists in dispatch but not in keybinding_parser, its user-configured keybinding is silently discarded. If it exists in keybinding_parser but not app_config_io, keybindings parse but are never persisted. There is no enforcement of parity between the three lists.

---

## Investigation Steps

- [x] Read all three files to extract the current action name sets
- [x] Compare them — identify any actions present in one list but absent from others
- [x] Document the discrepancies (likely candidates: `toggle_megacity_ui`, `edit_config`, newer actions)

---

## Test Design

A compile-time or runtime parity test that:
1. Extracts the set of action names from all three registries
2. Asserts the sets are identical (or that dispatch ⊇ keybinding ⊇ config, whichever is the correct policy)

```cpp
TEST_CASE("GUI action registries are consistent") {
    auto dispatch_actions = GuiActionHandler::known_actions(); // returns sorted vector<string>
    auto keybind_actions  = KeybindingParser::known_actions();
    auto config_actions   = AppConfigIo::known_gui_actions();
    REQUIRE(dispatch_actions == keybind_actions);
    REQUIRE(dispatch_actions == config_actions);
}
```

This requires adding a `known_actions()` (or equivalent) static accessor to each of the three classes. These can return a `const std::vector<std::string_view>&` of sorted action names.

---

## Implementation

- [x] Add `static std::vector<std::string_view> known_actions()` to `GuiActionHandler`
- [x] Add `static std::vector<std::string_view> known_actions()` to `KeybindingParser`
- [x] Add `static std::vector<std::string_view> known_gui_actions()` to `AppConfigIo`
- [x] Create `tests/gui_action_parity_test.cpp` with the parity assertion
- [x] Fix any discovered discrepancies (document them before fixing, or create separate WI if large)

---

## Acceptance Criteria

- [x] Parity test passes at HEAD
- [x] Any pre-existing discrepancies are either fixed or tracked as a separate bug WI
- [x] `known_actions()` accessors are const and have zero runtime overhead beyond the list itself
- [x] This test becomes a CI gate so future action additions that break parity are caught immediately

---

## Interdependency

**Must land before WI 71** (GUI action SSOT refactor). WI 71 will centralise these lists; this test is the safety net verifying the refactor is correct.
