---
# WI 71 — GUI Action Single Source of Truth

**Type:** refactor  
**Priority:** high (unblocks all future new GUI actions without three-way edit)  
**Raised by:** [C] Claude, [G] Gemini, [P] GPT (HIGH)  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

GUI action names exist in three independent registries:
1. `libs/draxul-config/src/app_config_io.cpp` — config key parsing
2. `libs/draxul-config/src/keybinding_parser.cpp` — keybinding name → enum mapping
3. `app/gui_action_handler.cpp` — action name → lambda dispatch

Adding a new action requires synchronized edits in all three. Missing one causes silent data loss (keybindings parsed but discarded, or dispatched but never persistable). [P] rates this HIGH.

---

## Investigation Steps

- [x] Read all three files; extract and compare the action name sets
- [x] Run WI 67 (parity test) first — it will make any existing discrepancies visible
- [x] Decide the canonical location for the action table. Options:
  - A new `GuiAction` enum + `kGuiActionNames[]` table in `libs/draxul-types/` (cleanest, header-only)
  - A static table in `libs/draxul-config/` consumed by both config and keybinding parsers
  - A registration API where `gui_action_handler.cpp` registers names at startup and both parsers query it

---

## Proposed Design

Preferred approach: a `GuiAction` enum and a `kGuiActionMeta` table in `libs/draxul-types/include/draxul/gui_actions.h`:

```cpp
enum class GuiAction : uint8_t {
    ToggleDiagnostics,
    Copy, Paste,
    FontIncrease, FontDecrease, FontReset,
    // ...
    Count
};

struct GuiActionMeta {
    GuiAction       action;
    std::string_view name;      // config key name
    std::string_view display;   // human-readable name in palette
};

inline constexpr GuiActionMeta kGuiActionMeta[] = {
    { GuiAction::ToggleDiagnostics, "toggle_diagnostics", "Toggle Diagnostics" },
    // ...
};
```

Then:
- `keybinding_parser.cpp` maps string → `GuiAction` by iterating `kGuiActionMeta`
- `app_config_io.cpp` maps string → `GuiAction` by iterating `kGuiActionMeta`
- `gui_action_handler.cpp` maps `GuiAction` → lambda via an array indexed by enum value

---

## Implementation Steps

- [x] Create `libs/draxul-types/include/draxul/gui_actions.h` with `GuiAction` enum and `kGuiActionMeta` table
- [x] Refactor `keybinding_parser.cpp` to use the table (delete the hand-rolled string list)
- [x] Refactor `app_config_io.cpp` to use the table
- [x] Refactor `GuiActionHandler` to dispatch by `GuiAction` enum rather than by string lookup
- [x] Update `GuiActionHandler::Deps` — 19 `std::function` fields can stay for now but are now indexed by `GuiAction` enum, making them easier to introspect
- [x] Update the parity test (WI 67) to verify via the table, not separate lists
- [x] Run full test suite; no behaviour change expected

---

## Acceptance Criteria

- [x] Single definition of all action names in `gui_actions.h`
- [x] Adding a new action requires only one edit: adding a row to `kGuiActionMeta` and a handler to `GuiActionHandler`
- [x] WI 67 parity test passes against the new table
- [x] Build succeeds on both platforms; smoke test passes

---

## Notes

**Subagent recommended** — this touches three library boundaries and `app/`. A single agent with full context of all three files is most efficient. Coordinate with WI 45 (pane management) which may be adding new actions concurrently.

**Interdependency:** WI 67 must land (or at least be drafted) before this refactor so the parity test catches any mistake in the migration.
