# Preserve nonoverlapping wide characters

**Summary:** Preserve a nearby wide character when updating a narrow character that does not overlap it.

**Priority:** P1  
**Source:** `libs/draxul-grid/src/grid.cpp`

**Evidence and trigger:** B13; writing a narrow character immediately before a wide leader clears that unrelated leader and continuation.

- [x] **Investigate:** Characterize narrow replacements, wide replacements, and writes over continuation cells.
  Confirmed in `Grid::set_cell`: any write at `col` cleared a wide leader at `col + 1` (and its continuation), although only a wide write overlaps that column. Narrow writes over a leader already clear its continuation; writes over a continuation already blank the leader.
- [x] **Fix:** Restrict neighboring-leader cleanup to actual overlap.
  The `col + 1` leader is displaced only when the new cell is stored as double width.
- [x] **Acceptance:** Updating the first cell of `" 界"` to `"A"` preserves the wide glyph; overlapping writes still remove orphaned continuations.
  `tests/grid_tests.cpp` "grid narrow writes preserve a wide neighbour they do not overlap"; the protocol path is also covered by `tests/ui_events_tests.cpp` "ui event handler keeps valid wide state across partial grid_line updates".
- [x] **Validation:** Run core aggregate tests and same-cache smoke.
  `do.py test debug`: only pre-existing/load failures (see `kanban/pending/88 nvim-wide-cell-protocol-decoding -bug.md` session notes); `do.py smoke debug --skip-build` passed.
