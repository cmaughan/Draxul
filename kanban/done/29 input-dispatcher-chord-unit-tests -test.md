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

- [x] Six new chord-state-machine TEST_CASEs added to `tests/input_dispatcher_routing_tests.cpp`
      under the `[chord]` tag.
- [x] Tests use the existing FakeWindow + StubHost harness — no real SDL events.
- [x] All new tests pass under `ctest` (24 assertions across 9 [chord] cases).

## Implementation

- [x] Read `app/input_dispatcher.{h,cpp}` — confirmed:
  * Single `prefix_active_` bool, no timeout, no state enum.
  * `is_modifier_only_key()` keeps the chord armed across SDL's modifier-only down events.
  * `set_host()` calls `on_focus_lost()` on the previous host but does **not** clear
    `prefix_active_` — documented as a finding (test pins down current behaviour).
- [x] Extended `tests/input_dispatcher_routing_tests.cpp` with a `ChordE2ESetup` harness
  that wires `GuiActionHandler::Deps::on_split_vertical` to a counter, then drives
  `FakeWindow::on_key` with raw `KeyEvent`s.
- [x] Six new TEST_CASEs covering:
  1. Happy path: prefix → matching second key fires action, second key not forwarded.
  2. Unrecognised second key: chord cancels, second key reaches host.
  3. Prefix-then-prefix: not re-armed; second prefix forwarded; chord no longer fires.
  4. Modifier-only key during prefix does not cancel — chord still resolves.
  5. State cleared after success — bare second key alone is inert.
  6. `set_host()` mid-chord does not clear state (focus-loss observation).
- [x] Build: `cmake --build build --target draxul-tests`
- [x] Run: `./build/tests/draxul-tests "[chord]"` — all 9 cases pass.

## Findings

- **No chord timeout exists.** The state machine is event-driven only. Tests assert
  this rather than waiting on a clock.
- **Focus loss does not clear chord state.** `InputDispatcher::set_host()` swaps the
  host pointer but leaves `prefix_active_` untouched, so a chord-second-key resolves
  against the new host. This is documented in
  `chord: switching focused host mid-chord does NOT clear prefix state` — if the
  desired UX is "cancel chord on focus change", a follow-up bug should be filed
  rather than silently flipping the test.

## Interdependencies

- **WI 41** (`cmake-configure-depends -refactor`) should land first so the new test file is
  automatically discovered.
- **WI 46** (`chord-prefix-visual-indicator -feature`) depends on this test coverage being in
  place before adding more chord state.
- Coordinate with icebox `01 chord-prefix-stuck -bug` — the test cases here may reveal whether
  the stuck-state bug has already been fixed.
