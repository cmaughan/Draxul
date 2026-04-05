# WI 95 — double-width-cont-col0-orphan

**Type:** bug  
**Priority:** 2 (glyph rendering corruption)  
**Source:** review-bugs-consensus.md §M2 [Claude]  
**Produced by:** claude-sonnet-4-6

---

## Problem

In `libs/draxul-grid/src/grid.cpp:177`, `set_cell()` clears the `double_width_cont` flag on the *previous* cell only when `col > 0`. A continuation cell left at column 0 (e.g., after a terminal resize that moves a double-width character off screen) is never cleared when that cell is subsequently overwritten. The orphaned `double_width_cont` flag causes the renderer to skip the cell's glyph or emit corrupted rendering for that column.

---

## Investigation

- [ ] Read `libs/draxul-grid/src/grid.cpp:165–215` — trace the full `set_cell()` logic for the `col == 0` case and confirm the continuation flag is never cleared on the target cell.
- [ ] Read `clear_continuation()` (called for `next` cell at line 192) — confirm whether it is safe to call on the current cell at col 0 as well.

---

## Fix Strategy

- [ ] After the `if (col > 0)` block and before writing the new cell content, unconditionally clear `cell.double_width_cont`:
  ```cpp
  cell.double_width_cont = false; // clear regardless of column
  ```
- [ ] Alternatively, call `clear_continuation(cell)` on the cell being written if that helper handles the general case.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`
- [ ] Run Unicode render test: `py do.py blessunicode` if snapshot changes, investigate.

---

## Acceptance Criteria

- [ ] A double-width character at column 0 followed by a resize and redraw no longer leaves orphaned `double_width_cont` state.
- [ ] Unicode render tests pass (or new snapshots are blessed if the fix produces visibly correct output).
- [ ] Smoke test passes.
