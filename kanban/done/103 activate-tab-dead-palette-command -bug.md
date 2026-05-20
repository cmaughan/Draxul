# WI 103 — activate-tab-dead-palette-command

**Type:** bug
**Priority:** 1 (HIGH — command palette lists an action that does nothing when selected)
**Source:** review-consensus.md §2 [GPT]
**Produced by:** claude-sonnet-4-6

---

## Problem

The command palette lists `activate_tab` as a plain, argument-free action. `GuiActionHandler::activate_tab()` is a parameterised action — without a numeric tab index it is a no-op. Selecting it from the palette does nothing; no error, no feedback.

The user sees an action in the palette, selects it, and nothing happens. This is confusing and erodes trust in the palette.

**Files:**
- `app/command_palette.cpp:29,177` — builds the action list shown in the palette
- `app/gui_action_handler.cpp:244` — `activate_tab()` requires a numeric argument

---

## Investigation

- [ ] Read `app/command_palette.cpp:1–200` — find where the action list is built; understand how actions are collected and displayed.
- [ ] Read `app/gui_action_handler.cpp:230–260` — confirm `activate_tab` is a no-op without an argument.
- [ ] Determine what other parameterised actions (if any) share the same problem.

---

## Fix Strategy

Choose one of:

**Option A — Expand parameterised actions into concrete entries:**
- [ ] In the palette build path, expand `activate_tab` into nine entries (`Activate Tab 1` … `Activate Tab 9`) each pre-bound to the numeric argument. Apply the same pattern to any other parameterised actions.

**Option B — Hide actions that require parameters:**
- [ ] Mark `activate_tab` (and similar) as non-palette-surfaceable in the action registry, or filter them out when building the palette list. Document that keybindings are the only way to use parameterised actions.

**Option C — Show with a hint that an argument is needed:**
- [ ] Display `activate_tab` in the palette with a suffix like `(requires: tab number)` and open an inline input prompt when selected.

Option A is the most user-friendly; Option B is the safest quick fix if A is out of scope.

- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke: `py do.py smoke`

---

## Acceptance Criteria

- [ ] `activate_tab` in the command palette either does something useful when selected or is not visible.
- [ ] No other parameterised actions appear as bare entries that silently no-op.

---

## Interdependencies

- **WI 102** (tab-keybinding-config-allowlist) — both concern tab action handling; address together.
- **WI 71** (gui-action-single-source-of-truth) — palette action metadata is a downstream concern of the action registry; WI 71 may provide a clean place to encode parameterisation metadata.
