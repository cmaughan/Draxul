# 01 chord-prefix-release-cancel -bug

**Priority:** Medium
**Type:** Bug / UX polish

## Current State

Chord prefix mode no longer stays armed forever: `InputDispatcher::update()` times out a pending prefix, and `tests/input_dispatcher_routing_tests.cpp` covers the timeout and indicator fade behavior.

What remains is a narrower UX issue. Releasing the prefix key without pressing a chord key does not immediately cancel prefix mode; the dispatcher waits for the timeout. That is safe, but it can surprise users who tap the prefix accidentally and then type before the timeout expires.

## Goal

Cancel a pending chord immediately when the same physical prefix key is released and no chord key has been pressed.

## Tasks

- [ ] Read `app/input_dispatcher.cpp` around the chord state machine and key-release path.
- [ ] Track the key/modifier combination that armed the current prefix.
- [ ] On key release, cancel prefix mode only when the released key matches the active prefix key.
- [ ] Keep modifier-only intermediate keys from breaking valid chords.
- [ ] Preserve existing timeout behavior as a fallback for missed key-up events or focus loss.
- [ ] Add a focused test in `tests/input_dispatcher_routing_tests.cpp`.

## Acceptance Criteria

- Tapping and releasing the prefix key cancels prefix mode immediately.
- Prefix down -> chord key down still fires the chord action.
- Prefix down -> modifier-only key events still allow a modified chord key.
- Existing chord timeout and fade tests continue to pass.
