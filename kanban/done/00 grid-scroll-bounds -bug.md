# Grid Scroll Out-of-Bounds

**Type:** bug
**Priority:** 00
**Raised by:** GPT

## Summary

`Grid::scroll` in `libs/draxul-grid/src/grid.cpp` uses the scroll delta directly in row/column index calculations without clamping it to the region dimensions. When the delta exceeds the region height or width, the resulting indices are out of bounds, producing heap corruption or a crash.

## Background

The neovim RPC protocol can send a `grid_scroll` event with a row count equal to or greater than the region height — this is a valid way to clear a region entirely. The current implementation trusts the incoming value and feeds it into array index arithmetic without bounds checking. Any delta larger than the region size will cause an out-of-bounds read or write into the grid cell array. This is the highest-severity bug found in the review: it is a real memory-safety risk present on any neovim usage pattern that scrolls a full viewport.

## Implementation Plan

### Files to modify
- `libs/draxul-grid/src/grid.cpp` — clamp `rows` to `(bot - top)` and `cols` to `(right - left)` before using them in index calculations in `Grid::scroll`
- `libs/draxul-grid/include/draxul/grid.h` — verify scroll method signature; add an assertion or DCHECK that the clamped value is applied

### Steps
- [x] Read the current `Grid::scroll` implementation and identify every index expression that uses the delta
- [x] Compute the effective region height (`bot - top`) and width (`right - left`)
- [x] Clamp the incoming delta to `[0, region_height]` (or `[0, region_width]` for horizontal scroll) before any index use
- [x] Verify that a clamped-to-full-region scroll correctly clears all cells in the region (same behaviour as filling with blank cells)
- [x] Confirm no other caller site passes a pre-clamped value that would break with the new clamping

## Depends On
- None

## Blocks
- `08 grid-scroll-bounds -test.md` — the test should be written after the fix is in place and verified

## Notes
Work item `08` covers adding a regression test for this exact path. Fix this bug first, then write the test so it can assert the corrected behaviour.

> Work item produced by: claude-sonnet-4-6
