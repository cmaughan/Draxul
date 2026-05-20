# 01 grid-line-bounds-check -bug

**Priority:** HIGH
**Type:** Bug
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`handle_grid_line()` in `libs/draxul-nvim/src/ui_events.cpp:187–200` extracts `row` and `col_start` from the msgpack payload without checking them against the current grid dimensions. A malformed or crafted RPC message with out-of-range coordinates causes an out-of-bounds memory write into the grid cell array — undefined behaviour and a potential security boundary violation.

Related: `grid.cpp:54` uses `int index = row * cols_ + col` with signed int arithmetic. A very large `cols_` from a malformed RPC resize can also overflow this product.

---

## Fix Plan

- [x] Read `libs/draxul-nvim/src/ui_events.cpp` around `handle_grid_line()`.
- [x] Read `libs/draxul-grid/src/grid.cpp` around the index calculation and grid bounds.
- [x] In `handle_grid_line()`, after extracting `row` and `col_start`, assert/guard that:
  - `row >= 0 && row < grid.rows()`
  - `col_start >= 0 && col_start < grid.cols()`
  - Each subsequent `col_start + offset` stays in range
  - Log a WARN and return early (do not crash) on violation.
- [x] In `grid.cpp` index calculation, change to `size_t` arithmetic and add a bounds check that returns early (or asserts in debug) on overflow/OOB.
- [x] Build and run smoke test + ctest.

---

## Acceptance

- Feeding a `grid_line` event with row or col outside grid dimensions: no crash, no memory corruption, WARN log emitted.
- Normal grid_line events continue to work correctly.

---

## Interdependencies

- Validates via **07-test** (grid-line-oob-coordinate).
- No upstream dependencies.
