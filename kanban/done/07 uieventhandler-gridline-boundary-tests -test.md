# 07 uieventhandler-gridline-boundary-tests -test

**Priority:** MEDIUM
**Type:** Test (regression guard for grid_line boundary fix)
**Raised by:** GPT
**Model:** claude-sonnet-4-6

---

## Problem

The `grid_line` replay boundary bug (`02-bug`) has no tests that exercise malformed repeat counts or wide-glyph overruns at the right edge of the grid. The `replay_fixture.h` builders make it easy to construct these scenarios without spawning Neovim.

---

## Implementation Plan

- [x] Read `tests/support/replay_fixture.h` — understand the builders for `grid_line` cell batches and full `redraw` event vectors.
- [x] Read `libs/draxul-nvim/src/ui_events.cpp` handler to understand what a valid vs. malformed `grid_line` looks like.
- [x] Write test cases using `replay_fixture.h`:
  - [x] **Normal right-edge cell** — `col_start + cells` exactly fills the row; all cells written; no crash.
  - [x] **Repeat overrun** — `col_start + (cells * repeat)` exceeds grid width; handler clamps cleanly; no out-of-bounds access.
  - [x] **Wide-glyph at last column** — a double-width character whose second half would fall past column `width - 1`; handler truncates and writes a placeholder space cell.
  - [x] **Combined: repeat + wide overrun** — `repeat` and wide-glyph both contribute to overrun; same clamping behavior.
- [x] Use a fake `IGridSink` that asserts no `set_cell()` calls arrive with `col >= grid_width`.
- [x] Add tests to an existing `ui_events_tests.cpp` or create a new file.
- [x] Build and run: `cmake --build build --target draxul-tests && ctest`.

---

## Acceptance

- Tests pass after the `02-bug` fix is applied.
- The fake `IGridSink` assertion fires on the unfixed code, proving the tests cover the actual bug.

---

## Interdependencies

- `02-bug` (grid_line replay boundary) — write alongside or immediately after the fix.

---

*claude-sonnet-4-6*
