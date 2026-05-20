# 01 mouse-modifier-drag-wheel -bug

**Priority:** HIGH
**Type:** Bug (incorrect protocol output)
**Raised by:** GPT-4o (primary), confirmed by Claude
**Model:** claude-sonnet-4-6

---

## Problem

Terminal mouse reporting encodes modifier state (Shift/Alt/Ctrl) in the protocol byte for button press and release events, but `mouse_reporter.cpp:69` does not accept or forward modifier bits for motion events (drag) or wheel events. Any terminal application relying on modifier+drag (e.g., Shift+drag for text selection in some apps) or modifier+scroll (e.g., Ctrl+scroll to zoom in many CLI tools) receives wrong reports.

---

## Code Locations

- `libs/draxul-host/include/draxul/mouse_reporter.h:44` — `report_motion()` / `report_wheel()` signatures
- `libs/draxul-host/src/mouse_reporter.cpp:69` — motion/wheel encoding path missing modifier bits
- `libs/draxul-host/src/local_terminal_host.cpp:132` — call site that passes motion/wheel events

---

## Implementation Plan

- [x] Read `mouse_reporter.h` and `mouse_reporter.cpp` to see the full function signatures for `report_motion`, `report_button`, and `report_wheel`.
- [x] Read `local_terminal_host.cpp` around line 132 to see how SDL mouse events are translated before being passed to the reporter.
- [x] Identify where SDL provides modifier state (`SDL_GetModState()` or the event `mod` field).
- [x] Update `report_motion()` signature to accept a `SDL_Keymod` (or equivalent bitmask) modifier parameter.
- [x] Update `report_wheel()` signature similarly.
- [x] In the encoding logic, OR the modifier bits into the protocol byte using the same encoding already used for button events (Shift = bit 2, Meta = bit 3, Ctrl = bit 4 in X10 extended encoding).
- [x] Update the call sites in `local_terminal_host.cpp` to pass the current modifier state.
- [x] Build: `cmake --build build --target draxul draxul-tests && py do.py smoke`
- [x] Run `clang-format` on all modified files.

---

## Acceptance Criteria

- A Ctrl+scroll event reports the Ctrl modifier bit in the protocol byte.
- A Shift+drag event reports the Shift modifier bit.
- Unmodified drag and wheel continue to report correctly (no regression).
- The regression tests in `07 mouse-drag-modifier-state -test` pass.

---

## Interdependencies

- **`07 mouse-drag-modifier-state -test`** — write the test alongside this fix.
- No upstream blockers.

---

*claude-sonnet-4-6*
