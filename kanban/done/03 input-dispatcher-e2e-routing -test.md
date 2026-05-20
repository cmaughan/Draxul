# 03 input-dispatcher-e2e-routing -test

**Type:** test
**Priority:** 3
**Source:** GPT review (review-latest.gpt.md); also implied by Claude (InputDispatcher under-tested)

## Problem

`InputDispatcher` is one of the most complex state machines in the codebase. It handles:
- GUI keybinding prefix mode (chord prefix stuck is an icebox bug)
- Text suppression during prefix wait
- Mouse routing (clicks, wheel, drag) including pixel-scale conversion
- File-drop dispatch into `NvimHost`
- Text editing event forwarding

The existing `input_dispatcher_routing_tests.cpp` constructs the dispatcher with null dependencies and only tests `gui_action_for_key_event()`. The most failure-prone paths — end-to-end through `connect()` with real fake objects — are barely covered.

GPT specifically flagged: file-drop coverage in `file_drop_tests.cpp` reimplements the encoding/decoding locally, so the wiring between `input_dispatcher.cpp` and `nvim_host.cpp` is untested end-to-end.

## Acceptance Criteria

- [x] Read `app/input_dispatcher.h`, `app/input_dispatcher.cpp`, and the existing `tests/input_dispatcher_routing_tests.cpp`.
- [x] Use `FakeWindow`, `FakeRenderer`, and a mock/fake host to call `dispatcher.connect(...)` and exercise the full dispatch path.
- [x] Add tests for:
  - [x] A key event that matches a GUI action reaches the action handler and is NOT forwarded to the host.
  - [x] A key event that does NOT match a GUI action is forwarded to the host.
  - [x] A file-drop event dispatches through `InputDispatcher` into a fake host's `on_files_dropped()` (or equivalent), verifying the wiring — not just the encoding logic.
  - [x] Mouse click events with pixel-scale applied produce correct grid-cell coordinates at the host.
  - [x] Text-editing events are forwarded consistently with the (now fixed, after item 02) contract.
- [ ] All new tests run under `ctest` and pass under `mac-asan` preset.

## Implementation Notes

- Use the existing `FakeWindow` / `FakeRenderer` / `FakeHost` patterns from the test suite — do not write new fakes unless the existing ones lack required interface surface.
- Prefer testing through the public `connect()` / dispatch interface, not internal methods.
- A sub-agent is well-suited for this: read the dispatcher, the existing tests, the fake objects, then write new test cases.

## Interdependencies

- **Depends on:** `02 input-dispatcher-contract-drift -bug` (test the correct contract, not the broken one).
- No other blockers.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
