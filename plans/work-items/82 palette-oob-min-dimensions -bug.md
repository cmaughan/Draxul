# WI 82 — palette-oob-min-dimensions

**Type:** bug  
**Priority:** 0 (crash / heap corruption — highest)  
**Source:** review-bugs-consensus.md §C1 [GPT]  
**Produced by:** claude-sonnet-4-6

---

## Problem

`render_palette()` (`libs/draxul-gui/src/palette_renderer.cpp:80`) has a guard that only rejects `grid_rows <= 0` or `grid_cols <= 0`, but the renderer logic requires at minimum 2 rows (entry area + separator + input) and 5 columns (padding + prompt + cursor). Below these thresholds:

- **Rows:** `sep_local_row = layout.rows - 2 = -1` when `grid_rows == 1`; `at(c, -1)` casts the negative index to `size_t` → out-of-bounds write before the grid vector.
- **Cols:** `query_max = layout.cols - 5 < 0` when `grid_cols < 5`; `cursor_local_col` can be negative, triggering the same cast.

Both paths corrupt heap memory and will crash or produce silent data corruption. Triggered by opening the command palette with the window height equal to one cell, or narrowed to ≤ 4 columns.

---

## Investigation

- [ ] Read `libs/draxul-gui/src/palette_renderer.cpp` fully — confirm the exact guard at line 81, the `at()` helper at line 108, and all negative-index paths.
- [ ] Read `app/command_palette_host.cpp:84–101` — confirm how `grid_rows` / `grid_cols` are computed from pixel dimensions and that values of 0 and 1 are reachable.
- [ ] Verify that `view_state()` in `libs/draxul-gui/include/draxul/gui/palette.h` also does not need a matching guard.

---

## Fix Strategy

- [ ] At the top of `render_palette()`, before `compute_layout()`, add:
  ```cpp
  if (state.grid_rows < 2 || state.grid_cols < 5) return {};
  ```
- [ ] Optionally add a debug-level log when the dimensions are below threshold so tests can detect it.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] `render_palette()` returns an empty vector (no-op) when called with `grid_rows < 2` or `grid_cols < 5`.
- [ ] No negative-index cast is possible through the `at()` helper for any valid input.
- [ ] Smoke test passes.
- [ ] No existing render-test references break.

---

## Interdependencies

- Touches `palette_renderer.cpp` — coordinate with WI 50 (overlay-input-routing) if open, to avoid merge conflicts.
