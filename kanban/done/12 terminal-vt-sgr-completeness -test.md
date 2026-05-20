# Terminal VT / SGR Completeness Test

**Type:** test
**Priority:** 12
**Raised by:** Claude

## Summary

Add a comprehensive test suite for `TerminalHostBase` VT sequence handling, covering SGR attributes that are commonly omitted in incomplete implementations: SGR 39/49 (default foreground/background), SGR 22/23/24/29 (attribute resets), `\e[?1049h/l` (alt-screen), `\e[?7h/l` (line wrap mode), and origin mode. This test suite can be developed by a dedicated sub-agent.

## Background

Terminal emulator completeness is a known area of subtle bugs. Missing SGR codes cause rendering artefacts that are hard to reproduce without a broad test matrix. Claude's review identified several specific SGR codes likely to be unimplemented. A comprehensive, table-driven test suite documents the expected behaviour and prevents regressions as the VT parser evolves.

**Sub-agent note:** This test suite is well-suited to a dedicated sub-agent. The work is self-contained (no external dependencies), the expected outputs are fully specified by the VT/ANSI standard, and the scope is large enough to benefit from parallel development. A sub-agent should write the full suite in one pass.

## Implementation Plan

### Files to modify
- `tests/terminal_vt_tests.cpp` — extended with new SGR and mode tests (no new file needed)

### Steps
- [x] Write SGR tests:
  - [x] SGR 0 — reset all attributes
  - [x] SGR 1 / 22 — bold on / bold off (not faint)
  - [x] SGR 3 / 23 — italic on / italic off
  - [x] SGR 4 / 24 — underline on / underline off
  - [x] SGR 9 / 29 — strikethrough on / strikethrough off
  - [x] SGR 30–37, 90–97 — foreground colour
  - [x] SGR 39 — default foreground colour (must reset to default, not black)
  - [x] SGR 40–47, 100–107 — background colour
  - [x] SGR 49 — default background colour
  - [x] SGR 38;2;r;g;b — 24-bit foreground
  - [x] SGR 48;2;r;g;b — 24-bit background
  - [x] SGR 38;5;n / 48;5;n — 256-colour palette
- [x] Write mode tests:
  - [x] `\e[?1049h` / `\e[?1049l` — alt-screen enter/exit preserves main-screen content
  - [x] `\e[?7h` / `\e[?7l` — line wrap on/off: character at column N wraps or overwrites
  - [x] Origin mode (`\e[?6h/l`) — cursor addressing relative to scroll region
- [x] Write cursor movement tests adjacent to above modes
- [x] Register all tests with ctest (added to draxul-tests in tests/CMakeLists.txt)

## Depends On
- None (can be written before or after `05 bracketed-paste-missing -bug.md`)

## Blocks
- None

## Notes
Use a table-driven test pattern: `(input_sequence, initial_state, expected_cell_attributes_or_output)`. This makes it easy to add new cases without boilerplate. The test harness should feed bytes into `TerminalHostBase` and inspect the resulting cell grid state.

> Work item produced by: claude-sonnet-4-6
