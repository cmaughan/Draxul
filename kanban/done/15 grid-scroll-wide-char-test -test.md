# 15 Grid Scroll Wide-Char Boundary Test

## Why This Exists

The `Grid::scroll()` repair pass for wide-character pairs has an edge case: wide-char pairs that straddle the horizontal boundary `[left, right)` of a scroll region may be incorrectly repaired (cells outside the region clobbered). See work item 11 for the associated fix.

This test should be written alongside that fix so the behaviour is validated and regressions are caught.

**Source:** `libs/draxul-grid/src/grid.cpp` — `Grid::scroll()`.
**Raised by:** Claude.

## Goal

Add regression tests for wide-character boundary handling in `Grid::scroll()`:
1. Wide-char pair entirely inside region → scroll correctly, no adjacent clobbering.
2. Wide-char leader at `right - 1` → leader is cleared within region; continuation at `right` is **not** touched.
3. Wide-char continuation at `left` → continuation is repaired (cleared to space); leader at `left - 1` is **not** touched.
4. Normal scroll of a region with `left = 0` (full width) → all wide pairs repaired correctly.

## Implementation Plan

- [x] Read `libs/draxul-grid/src/grid.cpp` and `include/draxul/grid.h` to confirm the Grid API and repair semantics.
- [x] Read existing grid tests (`tests/grid_tests.cpp`) for patterns.
- [x] Wrote the paired boundary regressions in `tests/grid_tests.cpp`, covering:
  - [x] wide pair fully inside a partial region
  - [x] leader repaired at the right boundary without touching outside-region continuation state
  - [x] continuation repaired at the left boundary without touching outside-region columns
  - [x] full-width repair of an orphaned continuation
  - [x] no clobbering of outside-region wide leaders
- [x] Added the tests to `tests/grid_tests.cpp`.
- [x] Ran `ctest --test-dir build --build-config Release --output-on-failure`.

## Notes

This test item is paired with bug fix [[11 grid-scroll-wide-char-boundary -bug]].
Coordinate writing this test and the fix together in the same agent session.

## Sub-Agent Split

Single agent. All changes in `tests/grid_tests.cpp`.
