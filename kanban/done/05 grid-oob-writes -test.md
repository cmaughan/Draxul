# Test: Grid Out-of-Bounds Write Safety Tests

**Type:** test
**Priority:** 5
**Source:** Claude review

## Problem

`Grid::set_cell()` and `Grid::scroll()` both contain early-return guards for invalid coordinates, but no test explicitly exercises these guards. A regression could remove the guard, leading to a silent OOB write or a crash under ASan.

## Investigation steps

- [x] Read `libs/draxul-grid/src/grid.cpp` — find every bounds check in `set_cell()` and `scroll()`.
- [x] Read `libs/draxul-grid/include/draxul/grid.h` — check the `GridRegion` struct and any related types.
- [x] Check existing grid tests in `tests/` for overlap.

## Test design

Add tests to `tests/grid_tests.cpp` (or create `tests/grid_bounds_tests.cpp` if the existing file is large).

### `set_cell()` boundary tests

- [x] `row == -1` → no crash, no dirty flag set.
- [x] `row == rows_` (one past end) → no crash.
- [x] `col == -1` → no crash.
- [x] `col == cols_` (one past end) → no crash.
- [x] `row` and `col` both valid → cell is updated and dirty flag is set.

### `scroll()` boundary tests

- [x] Scroll delta exceeds region height → no crash, region cleared or handled consistently.
- [x] Scroll region with `top > bottom` → no crash.
- [x] Zero-row scroll region → no-op, no crash.
- [x] Negative scroll delta equal to region height → no crash.
- [x] Valid scroll → correct rows moved, vacated rows cleared.

### Dirty-flag integrity

- [x] After any OOB call, verify the grid's dirty bitmask is not corrupted (no extra dirty bits set).

## Acceptance criteria

- [x] All the above test cases pass under `mac-asan` (ASan + UBSan).
- [x] Tests added to the `draxul-tests` CMake target and run via `ctest`.

## Interdependencies

- **`01 grid-index-overflow-ub -bug`**: fix the UB first so these tests pass cleanly under ASan.
- **`17 grid-scroll-ops-split -refactor`**: the scroll split may change function signatures; coordinate.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
