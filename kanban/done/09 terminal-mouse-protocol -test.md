# Terminal Mouse Protocol Test

**Type:** test
**Priority:** 09
**Raised by:** GPT

## Summary

Add unit tests for the terminal mouse event reporting path in `TerminalHostBase`, covering DECSET 1002 (button-motion reporting) and DECSET 1003 (all-motion reporting). Tests should verify that drag events are correctly emitted after a button press and that events are suppressed when the relevant mode is not active.

## Background

Work item `01` fixes the broken mouse drag implementation. These tests provide regression coverage and document the expected behaviour of each mouse mode. The tests exercise the escape sequence output produced by `TerminalHostBase` in response to synthesised mouse events, allowing the protocol logic to be verified without a live terminal application.

## Implementation Plan

### Files to modify
- `libs/draxul-host/tests/` (or equivalent test directory) — add `terminal_mouse_test.cpp`
- `libs/draxul-host/CMakeLists.txt` — register with ctest

### Steps
- [x] Write test: DECSET 1000 active, button press → correct `\e[M` encoding emitted
- [x] Write test: DECSET 1000 active, button release → correct encoding emitted
- [x] Write test: DECSET 1000 active, motion (no button) → no event emitted
- [x] Write test: DECSET 1002 active, button press → event emitted; motion without button → no event; motion with button held → event emitted
- [x] Write test: DECSET 1002 active, button release → event emitted; subsequent motion → no event
- [x] Write test: DECSET 1003 active, motion without button → event emitted
- [x] Write test: no mouse mode active, all event types → no output
- [x] Write test: DECSET 1006 (SGR mouse) encoding for coordinates > 223
- [x] Register all tests with ctest

## Depends On
- `01 terminal-mouse-drag -bug.md` — the fix should be in place before writing tests against correct behaviour

## Blocks
- None

## Notes
The test harness should drive `TerminalHostBase` by sending VT escape sequences (to enable mouse modes) and synthetic `MouseEvent` objects, then capture the bytes written to the terminal input stream and assert their content against the expected escape encoding.

> Work item produced by: claude-sonnet-4-6
