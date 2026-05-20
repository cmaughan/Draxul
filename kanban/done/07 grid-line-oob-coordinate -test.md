# 07 grid-line-oob-coordinate -test

**Priority:** HIGH
**Type:** Test
**Raised by:** Claude (validates 01-bug fix)
**Model:** claude-sonnet-4-6

---

## Problem

No test feeds a `grid_line` event with row or col outside the current grid dimensions and verifies that no crash or memory corruption occurs. This test is listed in Claude's review as the #1 missing stability test.

---

## Implementation Plan

- [x] Read `tests/support/replay_fixture.h` to understand the msgpack builder helpers.
- [x] Read `libs/draxul-nvim/src/ui_events.cpp` around `handle_grid_line()` to understand the parsing path.
- [x] Write a test in `tests/grid_line_bounds_tests.cpp` (or add to existing grid tests):
  - Construct a Grid of known size (e.g., 80×24).
  - Build a `redraw` event with a `grid_line` payload where `row = 9999` (way out of range).
  - Feed the event through the UiEventHandler.
  - Assert: no crash, no ASAN error, a WARN log is emitted.
  - Repeat with `col_start` out of range.
  - Repeat with negative values if the type allows it.
- [x] Also test the integer overflow case: a grid resize to `cols = 65536`, then a `grid_line` with `col_start` near max int — verify no signed overflow.
- [x] Add the new file to `tests/CMakeLists.txt`.
- [x] Run with ASan preset: `cmake --preset mac-asan && cmake --build build --target draxul-tests && ctest --test-dir build -R draxul-tests`.

---

## Acceptance

- OOB grid_line events produce no crash, no memory corruption, and a WARN log.
- Normal grid_line events still render correctly.

---

## Interdependencies

- Best run after **01-bug** fix. Can be written before fix as TDD — test should fail (crash or ASAN) before fix, pass after.
