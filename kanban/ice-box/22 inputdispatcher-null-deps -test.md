# WI 22 — InputDispatcher with null dependency callbacks

**Type:** test  
**Source:** review-latest.claude.md  
**Consensus:** review-consensus.md Phase 5

---

## Goal

Verify that constructing `InputDispatcher` with one or more `Deps` fields set to `nullptr` produces graceful no-ops (not crashes or silent corruption) for every event type. This guards against the `Deps` bloat problem where a missing callback silently breaks input.

---

## What to test

For each of the 20+ callbacks in `InputDispatcher::Deps`:
- [ ] Construct an `InputDispatcher` with exactly one callback set to `nullptr`, all others valid.
- [ ] Fire every input event type: key press, key release, mouse move, mouse click (left/right/middle), scroll, text input, drag.
- [ ] Assert: no crash, no `nullptr` dereference, no silent state corruption.
- [ ] Assert: events that can still be handled (because the nullptr callback is not on the hot path for that event) are handled correctly.

**Specific high-value cases:**
- [ ] `tab_bar_height_phys` = nullptr → mouse events near the tab bar return gracefully.
- [ ] `hit_test_tab` = nullptr → clicks in the tab area are ignored gracefully.
- [ ] `hit_test_pane_pill` = nullptr → pane pill clicks are ignored gracefully.

---

## Implementation notes

- The `Deps` struct has 20+ fields; a parameterised test (or a loop over a table of null positions) is more maintainable than 20+ individual tests.
- Run under ASan — null-deref bugs must not pass ASan.
- This test effectively documents the valid null combinations; add a comment in `input_dispatcher.h` for each callback indicating whether null is a valid "disabled" value.
- Place in `tests/input_dispatcher_null_deps_test.cpp`.

---

## Interdependencies

- WI 26 (inputdispatcher-routing-consolidation refactor) may reduce the Deps count; write this test first to lock down the current contract before the refactor changes it.
- WI 10 icebox (inputdispatcher-focus-loss) is a related test covering focus state; both can share harness infrastructure.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
