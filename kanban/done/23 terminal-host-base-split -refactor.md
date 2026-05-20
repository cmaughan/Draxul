# TerminalHostBase God Class Split

**Type:** refactor
**Priority:** 23
**Raised by:** GPT, Claude, Gemini

## Summary

`TerminalHostBase` is a God class that combines VT sequence parsing, scrollback buffer management, selection handling, mouse protocol reporting, alt-screen switching, clipboard integration, and process back-end management in a single class. Split it into focused, single-responsibility components. This is the most widely-agreed architectural finding across all three reviews.

**Sub-agent note:** This refactor is large, well-isolated, and ideal for a dedicated sub-agent. The extraction targets are clearly defined, the class boundaries are stable, and the work does not require simultaneous changes to other parts of the codebase.

## Background

A God class is the most common source of long-term maintenance debt. In `TerminalHostBase`, the coupling between unrelated concerns (e.g., selection state and VT parsing) means that a bug in one area requires understanding the entire class. It also makes testing nearly impossible because there is no seam to inject mocks or observe outputs. The three agents unanimously agree this is the highest-priority architectural issue.

## Implementation Plan

### Files to modify
- `libs/draxul-host/src/terminal_host_base.cpp` — source of extraction
- `libs/draxul-host/src/terminal_host_base.h` — source of extraction
- New files to create:
  - `libs/draxul-host/src/vt_parser.cpp` / `vt_parser.h` — VT/ANSI escape sequence parsing and dispatch
  - `libs/draxul-host/src/scrollback_buffer.cpp` / `scrollback_buffer.h` — scrollback ring buffer, line storage, viewport offset
  - `libs/draxul-host/src/selection_manager.cpp` / `selection_manager.h` — selection state, anchor/extent tracking, copy-to-clipboard
  - `libs/draxul-host/src/mouse_reporter.cpp` / `mouse_reporter.h` — DECSET mouse mode state, event encoding, escape sequence emission
  - `libs/draxul-host/src/alt_screen_manager.cpp` / `alt_screen_manager.h` — main/alt screen swap, snapshot management

### Steps
- [x] Audit `terminal_host_base.cpp` and classify every method and data member into one of the five responsibility areas
- [x] Extract `ScrollbackBuffer` class: ring buffer, FIFO eviction, viewport offset, line access by visual row
- [x] Extract `SelectionManager` class: anchor/extent state, cell collection, clipboard write
- [x] Extract `MouseReporter` class: mode flags (DECSET 1000/1002/1003/1006), button-state tracking, escape sequence encoding
- [x] Extract `AltScreenManager` class: main-screen snapshot, enter/exit logic, resize-aware snapshot update
- [x] Extract `VtParser` class (or use existing if one exists): character-by-character state machine, calls back into the host for each decoded action
- [x] Wire the extracted components back into a slimmed-down `TerminalHostBase` that owns and delegates to them
- [x] Run `ctest` to verify all existing tests pass (pre-existing alt-screen cursor test failure unchanged)
- [x] Run the render smoke tests (`python do.py smoke`) to verify visual output is unchanged

## Depends On
- `01 terminal-mouse-drag -bug.md` — fix mouse drag before splitting, to avoid merge conflicts
- `05 bracketed-paste-missing -bug.md` — fix bracketed paste before splitting
- `06 altscreen-resize-mismatch -bug.md` — fix alt-screen resize before splitting

## Blocks
- `31 app-class-decomposition -refactor.md` — App decomposition should learn from this refactor's patterns

## Notes
Do the extraction incrementally: one class at a time, passing all tests before moving to the next. Do not attempt to extract all five classes in a single commit. Use the test suite (work items 09, 12, 13, 14, 15) as a safety net — write those tests before or alongside this refactor.

> Work item produced by: claude-sonnet-4-6
