# Refactor: Split Grid::scroll() into scroll_rows / scroll_cols

**Type:** refactor
**Priority:** 17
**Source:** Claude review

## Problem

`Grid::scroll()` in `libs/draxul-grid/src/grid.cpp` has four sibling if-branches for the ±row and ±col cases, each with nested loops. The function is ~126 lines and handles all four scroll directions in a single body. This makes it:

- Hard to read: you must track which branch you are in to understand any one path.
- Hard to test: boundary conditions for row-scroll and column-scroll are unrelated but must be set up together.
- A merge-conflict magnet: any change to one direction risks touching the other three.

## Proposed design

Extract two free (or private static) functions:

```cpp
// Scroll the cells within `region` by `delta` rows.
// Positive delta = scroll up (content moves up, new rows appear at bottom).
// Negative delta = scroll down.
static void scroll_rows(std::vector<Cell>& cells, int cols,
                        const GridRegion& region, int delta);

// Scroll the cells within `region` by `delta` columns.
static void scroll_cols(std::vector<Cell>& cells, int cols,
                        const GridRegion& region, int delta);
```

`Grid::scroll()` becomes a thin dispatcher:

```cpp
void Grid::scroll(GridRegion region, int row_delta, int col_delta) {
    if (row_delta != 0) scroll_rows(cells_, cols_, region, row_delta);
    if (col_delta != 0) scroll_cols(cells_, cols_, region, col_delta);
}
```

The free functions can be in an anonymous namespace in `grid.cpp` — no public API change.

## Implementation steps

- [x] Read `grid.cpp` — fully understand all four branches in `scroll()`.
- [x] Extract `scroll_rows()` as a free function; verify its test cases manually.
- [x] Extract `scroll_cols()` as a free function; verify its test cases manually.
- [x] Replace the body of `Grid::scroll()` with the dispatcher.
- [x] Build and run existing grid tests to confirm no regression.
- [ ] Coordinate with `05 grid-oob-writes -test` — write the OOB test in the same pass.

## Acceptance criteria

- [x] `Grid::scroll()` body is ≤ 20 lines (dispatcher only).
- [x] `scroll_rows` and `scroll_cols` are separate named functions.
- [x] All existing grid tests pass.
- [x] No public API changes — only internal restructuring.

## Interdependencies

- **`05 grid-oob-writes -test`**: OOB tests are cleaner to write after the split; do in same agent pass.
- **`01 grid-index-overflow-ub -bug`**: fix overflow UB first; the overflow may be in the scroll loop body.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
