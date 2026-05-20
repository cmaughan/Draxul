# 05 grid-scroll-per-direction-tests -test

**Priority:** MEDIUM
**Type:** Test (grid correctness coverage)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`Grid::scroll()` is ~125 lines with four inline direction cases (up/down/left/right). The existing `grid_tests.cpp` covers some scroll cases but not the full matrix of directions × boundary conditions. Missing coverage includes: scroll at the start/end of a scroll region, multi-row scroll, and wide-character boundary repair in each direction.

---

## Implementation Plan

- [x] Read `libs/draxul-grid/src/grid.cpp` — understand `Grid::scroll()` and the four direction implementations.
- [x] Read `tests/grid_tests.cpp` — identify exactly which scroll scenarios are already covered so new tests are additive, not duplicative.
- [x] Write per-direction test cases for each of: `up`, `down`, `left`, `right`. For each direction, cover:
  - [x] Single-row/column scroll
  - [x] Multi-row/column scroll
  - [x] Scroll at the top/left boundary of the scroll region
  - [x] Scroll at the bottom/right boundary of the scroll region
  - [x] Wide-character boundary repair (a cell that would be split by a scroll becomes a space cell, not a half-glyph)
- [x] Add test cases to `tests/grid_tests.cpp` (or a new `tests/grid_scroll_tests.cpp` if the file is already large).
- [x] Build and run: `cmake --build build --target draxul-tests && ctest`.

---

## Acceptance

- All four scroll directions have coverage for all boundary conditions listed above.
- Wide-character repair is explicitly exercised in at least the up and down directions.
- All tests pass on a clean build.

---

## Interdependencies

- `10-test` (wide-char scroll render scenario) — the render scenario provides visual validation; these unit tests provide directional isolation.
- No upstream blockers.

---

*claude-sonnet-4-6*
