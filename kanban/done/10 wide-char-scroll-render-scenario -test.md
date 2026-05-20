# 10 wide-char-scroll-render-scenario -test

**Priority:** MEDIUM
**Type:** Test (visual regression guard for wide-character scroll repair)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`Grid::scroll()` repairs wide-character boundaries when a scroll would leave a half-width cell behind. This repair is critical for correct CJK rendering but has no deterministic visual regression test. A broken repair produces immediately visible corruption (half-glyph artifacts) in a BMP diff, making this an ideal render-scenario candidate.

---

## Implementation Plan

- [x] Read `app/render_test.cpp` and an existing render scenario `.toml` file to understand the scenario format.
- [x] Read `tests/scenarios/` (or wherever scenario files live) to understand naming and structure.
- [x] Design a deterministic scenario:
  - Grid dimensions: 40 columns × 10 rows.
  - Fill rows with CJK characters (e.g., U+4E00 through U+4E13, 20 wide characters × 2 cells each = 40 columns per row).
  - Scroll the grid up by 1 row (equivalent to `grid_scroll` with `top=0, bot=9, rows=1`).
  - Assert that no half-width artifact cells appear in the result (all cells are either a full wide character start, a wide-char second-cell placeholder, or a blank repair cell).
- [x] Write the `.toml` scenario file.
- [x] Run the scenario to generate an initial reference BMP: `py do.py blessbasic` (or the appropriate bless command for the new scenario name).
- [x] Add the scenario to the CI render test run and verify it appears in `do.py test`.
- [x] Build and run: `cmake --build build --target draxul && py do.py test`.

---

## Acceptance

- The render scenario produces a reference BMP with no half-glyph artifacts.
- A deliberate regression in `Grid::scroll()` wide-char repair causes the scenario to fail with a visible BMP diff.

---

## Interdependencies

- `05-test` (grid scroll per-direction unit tests) — complementary; this provides visual confirmation, those provide directional isolation.
- No upstream blockers.

---

*claude-sonnet-4-6*
