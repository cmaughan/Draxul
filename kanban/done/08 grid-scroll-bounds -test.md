# Grid Scroll Bounds Test

**Type:** test
**Priority:** 08
**Raised by:** GPT, Claude

## Summary

Add unit tests for `Grid::scroll` that exercise scroll deltas equal to, greater than, and less than the region height and width, confirming that the implementation never accesses memory out of bounds and that cells are correctly moved or cleared.

## Background

Work item `00` fixes a memory-safety bug where `Grid::scroll` indexes out of bounds when the delta exceeds the region size. This test item provides regression coverage for that fix and ensures future changes to the scroll path do not reintroduce the vulnerability. The tests should be runnable under AddressSanitizer (see work item `20`) to guarantee that any out-of-bounds access is caught.

## Implementation Plan

### Files to modify
- `libs/draxul-grid/` — add a new test file (e.g., `tests/grid_scroll_test.cpp`) or extend an existing grid test file
- `libs/draxul-grid/CMakeLists.txt` — register the new test target with ctest

### Steps
- [x] Create or extend a test file for `Grid`
- [x] Write test: scroll delta = 0 (no-op, no cell changes)
- [x] Write test: scroll delta = 1 (single row, normal case)
- [x] Write test: scroll delta = region_height - 1 (one row remains)
- [x] Write test: scroll delta = region_height (all rows scroll out, region should be filled with blank cells)
- [x] Write test: scroll delta > region_height (clamped to region_height, same result as above — no OOB)
- [x] Write test: scroll delta = region_width for horizontal scroll (if applicable)
- [x] Write test: scroll delta > region_width for horizontal scroll
- [x] Run tests under ASan to verify no memory errors (deferred to work item 20 asan-lsan-ci)
- [x] Register all new tests with ctest

## Depends On
- `00 grid-scroll-bounds -bug.md` — the fix must land before the test is written against correct behaviour

## Blocks
- None

## Notes
The tests should construct a small `Grid` (e.g., 10×10) with known cell content, call `scroll` with various deltas, and assert the resulting cell layout matches expectations. Use concrete cell values (not just defaults) so that incorrect shift amounts are immediately visible in failures.

> Work item produced by: claude-sonnet-4-6
