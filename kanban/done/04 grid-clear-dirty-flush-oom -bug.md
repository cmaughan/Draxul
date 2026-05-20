# WI 04 — grid-clear-dirty-flush-oom

**Type:** bug  
**Priority:** MEDIUM (OOM/stall on adversarial or fuzzed `grid_resize` RPC; no impact at normal terminal sizes)  
**Platform:** all  
**Source:** review-bugs-consensus.md — BUG-05 (Gemini, originally CRITICAL)

---

## Problem

`Grid::clear()` in `libs/draxul-grid/src/grid.cpp` (lines 268–278) does:
1. Clears `dirty_cells_` and resets all `dirty_marks_` to 0.
2. Iterates every cell and calls `mark_dirty_index(i)`.

Because dirty_marks_ are freshly zeroed, `mark_dirty_index` unconditionally pushes every index into `dirty_cells_`. With `kMaxGridDim = 10000`, a `grid_resize` event near maximum dimensions causes ~100 M `DirtyCell` pushes (≈800 MB allocation) and a long stall.

This is not triggered by any real Neovim session (realistic grids are ≤ 500×200), but a malformed or fuzzed `grid_resize` RPC message can reach this code path.

---

## Investigation

- [x] Read `libs/draxul-grid/src/grid.cpp` lines 246–279 to confirm `kMaxGridDim`, `Grid::clear()` body, and `mark_dirty_index()`.
- [x] Check `Grid::resize()` (lines 248–266): it calls `clear()` after resizing. Confirm resize also pushes all cells dirty.
- [x] Identify all callers of `Grid::clear()` and `Grid::resize()` to understand where the dirty list is consumed.
- [x] Review `GridRenderingPipeline::flush()` to understand how `get_dirty_cells()` is used and whether a `full_dirty_` flag would be feasible.

---

## Fix Strategy

- [x] Add a `bool full_dirty_` member to `Grid`.
- [x] In `Grid::clear()`, set `full_dirty_ = true` instead of calling `mark_dirty_index` per cell.
- [x] In `Grid::get_dirty_cells()`, when `full_dirty_` is set, populate and return all cell positions. Also exposed `is_full_dirty()` predicate.
- [x] Clear `full_dirty_` in `Grid::clear_dirty()`.
- [x] `mark_all_dirty()` also uses `full_dirty_` flag instead of per-cell loop.
- [x] `Grid::resize()` calls `clear()` which sets `full_dirty_` — no separate fix needed.

---

## Acceptance Criteria

- [x] `Grid::clear()` does not push 100 M entries into `dirty_cells_` for a large grid.
- [x] After `clear()`, all cells are treated as dirty by the renderer (no blank spots after a full clear).
- [x] Unit tests for `Grid` still pass: `ctest --test-dir build -R draxul-tests`.
- [x] Build and smoke test pass: `cmake --build build --target draxul draxul-tests && py do.py smoke`.

---

## Notes

- This change touches the interface between `Grid` and `GridRenderingPipeline`. Coordinate with any in-flight work touching `get_dirty_cells()`.
- The fix for `Grid::resize()` is subsumed by calling `clear()` which will set `full_dirty_`.
