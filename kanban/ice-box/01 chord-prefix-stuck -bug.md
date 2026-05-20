# 01 chord-prefix-stuck -bug

**Priority:** HIGH
**Type:** Bug (UX, input handling)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`InputDispatcher` has a chord-prefix mode: pressing the configured prefix key sets a flag, and the next key-down is interpreted as a chord action. However, if the user presses the prefix key and releases it without pressing a chord key (accidental activation, or UI focus lost between events), the dispatcher remains permanently in prefix mode. The next unrelated key-down is silently consumed as a (likely mismatched) chord lookup rather than passed through to Neovim. The code has a comment acknowledging this but leaves it unresolved.

---

## Fix Plan

- [ ] Read `libs/draxul-nvim/src/input_dispatcher.cpp` (or wherever chord prefix state lives) and understand the current state machine.
- [ ] Identify the key-release handler path (or the absence of one).
- [ ] Add a handler for the prefix key's `SDL_KEYUP` event that resets `prefix_active = false`.
  - Guard: only reset on the *same* key that activated the prefix, not any key-up.
  - Alternative: if the prefix is a modifier (Ctrl, Alt), reset on any key-up if the modifier is no longer held.
- [ ] Ensure the fix does not break normal chord sequences where the prefix key is held while the chord key is pressed.
- [ ] Build and run smoke test.

---

## Acceptance

- Pressing the prefix key and releasing without a chord key leaves the dispatcher in normal (non-prefix) state.
- Normal chord sequences (prefix down → chord key down → prefix up) still work.
- No key events are swallowed in non-prefix mode.

---

## Interdependencies

- `06-test` (InputDispatcher prefix-stuck test) — the test covers this fix and should be written in the same pass or immediately after.
- No upstream blockers.

---

*claude-sonnet-4-6*
