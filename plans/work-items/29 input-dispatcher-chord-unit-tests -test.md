# 29 input-dispatcher-chord-unit-tests -test

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.claude.md [C]*

## Problem

`app/input_dispatcher.cpp` implements a tmux-style chord prefix state machine.  The following
behaviours have no unit-test coverage:

- Pressing the prefix key then an **unrecognised** key (chord should cancel, second key should
  be forwarded to the active host).
- Pressing the prefix key then the **same prefix key again** (should it forward the prefix or
  treat as cancel?).
- A **modifier held** during the second key of a chord (e.g., chord prefix then `Ctrl+|`).
- Chord **timeout expiry** — if the implementation has a timeout, verify it fires correctly.
- **Focus loss** mid-chord (a window focus-lost event while the chord prefix is pending).

A regression in chord handling is invisible until a user tries a keybinding and finds it
forwarded to nvim instead of triggering the GUI action.

## Acceptance Criteria

- [ ] A new test file `tests/input_dispatcher_chord_tests.cpp` (or additions to an existing
      `input_dispatcher_tests.cpp`) covers the five scenarios above.
- [ ] Tests use the existing fake-window / fake-renderer infrastructure from
      `tests/support/` — no real SDL events.
- [ ] All new tests pass under `ctest`.

## Implementation Plan

1. Read `app/input_dispatcher.cpp` and `app/input_dispatcher.h` to understand the exact
   chord state machine (state enum, transition table, timeout if any).
2. Check `tests/` for any existing `input_dispatcher*` test file to add to rather than
   create a new one.
3. Write parametrised test cases for each scenario above, using `KeyEvent` fakes.
4. Verify the existing keybinding-conflict-detection tests (in complete) still pass.
5. Run `cmake --build build --target draxul-tests && ctest -R input_dispatcher`.

## Files Likely Touched

- `tests/input_dispatcher_chord_tests.cpp` (new, or additions to existing test file)
- `tests/CMakeLists.txt` — only if a new file is added **and** `CONFIGURE_DEPENDS` is not yet
  fixed (WI 41 should land first to avoid silent test-file miss).

## Interdependencies

- **WI 41** (`cmake-configure-depends -refactor`) should land first so the new test file is
  automatically discovered.
- **WI 46** (`chord-prefix-visual-indicator -feature`) depends on this test coverage being in
  place before adding more chord state.
- Coordinate with icebox `01 chord-prefix-stuck -bug` — the test cases here may reveal whether
  the stuck-state bug has already been fixed.
