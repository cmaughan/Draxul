# Terminal Mouse Drag Broken

**Type:** bug
**Priority:** 01
**Raised by:** GPT

## Summary

In `libs/draxul-host/src/terminal_host_base.cpp`, mouse button-press events return early without recording which button is currently held. Motion events check `sel_dragging_` (a selection-only flag) to decide whether to emit drag reports, so DECSET 1002/1003 mouse drag mode never emits motion events. Applications that rely on mouse drag — tmux mouse mode, ranger, midnight commander, htop — are silently broken.

## Background

The terminal mouse protocol has two relevant modes:
- DECSET 1002 (`\e[?1002h`) — report button-press, release, and motion-while-button-held
- DECSET 1003 (`\e[?1003h`) — report all motion events regardless of button state

The current implementation gates drag reporting on `sel_dragging_`, which is set only when the Draxul selection logic is active. A button press in a terminal application (e.g., clicking a tmux pane border) never sets `sel_dragging_`, so subsequent motion events are dropped. The fix requires: (1) tracking a `mouse_btn_pressed_` state that is set/cleared on button press/release events, and (2) gating drag emission on the active DECSET mouse mode flags rather than on `sel_dragging_`.

## Implementation Plan

### Files to modify
- `libs/draxul-host/src/terminal_host_base.cpp` — fix button-press handler to record pressed state; fix motion handler to check mouse mode flags
- `libs/draxul-host/src/terminal_host_base.h` — add `mouse_btn_pressed_` field (or a bitmask for multi-button tracking); ensure existing `sel_dragging_` logic is not disturbed for the Draxul-side selection feature

### Steps
- [x] Add a `uint8_t mouse_btn_pressed_` bitmask (one bit per button) to `TerminalHostBase`
- [x] In the button-press handler, set the corresponding bit in `mouse_btn_pressed_` before any early return
- [x] In the button-release handler, clear the corresponding bit
- [x] In the motion handler, check: if DECSET 1003 is active, always emit; if DECSET 1002 is active, emit only when `mouse_btn_pressed_ != 0`
- [x] Ensure the existing Draxul selection drag path (`sel_dragging_`) continues to work alongside the new tracking
- [ ] Test manually with a tmux session using mouse mode and verify pane border dragging works

## Depends On
- None

## Blocks
- `09 terminal-mouse-protocol -test.md`

## Notes
Be careful not to conflate the Draxul-side selection drag (`sel_dragging_`) with the terminal-side mouse protocol reporting. They share the motion event path but have different purposes. The fix should layer the mouse-mode check on top of the existing selection logic, not replace it.

> Work item produced by: claude-sonnet-4-6
