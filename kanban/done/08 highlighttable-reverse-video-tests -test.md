# 08 highlighttable-reverse-video-tests -test

**Priority:** MEDIUM
**Type:** Test (correctness coverage for complex attribute interaction)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`HighlightTable::resolve()` implements reverse-video attribute swapping, but the interaction between reverse-video and partial overrides (e.g., explicit fg with default bg, or explicit bg with default fg) is complex and currently has no targeted test. The normal→visual mode transition changes the effective highlight attributes of many cells simultaneously, making this a high-impact code path for rendering correctness.

---

## Implementation Plan

- [x] Read `libs/draxul-grid/include/draxul/grid.h` and `libs/draxul-grid/src/grid.cpp` — understand `HlAttr`, `HighlightTable`, and `resolve()`.
- [x] Read existing highlight-related tests in `tests/` to identify current coverage.
- [x] Write test cases for `HighlightTable::resolve()` covering:
  - [x] **Plain reverse-video** — `reverse = true`, default fg and bg → fg and bg are swapped.
  - [x] **Reverse-video + explicit fg** — `reverse = true`, explicit fg set, default bg → explicit fg becomes bg, default fg becomes fg.
  - [x] **Reverse-video + explicit bg** — `reverse = true`, default fg, explicit bg set → explicit bg becomes fg, default bg becomes bg.
  - [x] **Reverse-video + both explicit** — both fg and bg set → both are swapped.
  - [x] **Reverse-video disabled** — `reverse = false` with same attribute combinations → no swap.
  - [x] **Normal → Visual mode transition** — simulate a highlight ID change from a non-reverse to a reverse-video entry and verify the resolved colors flip.
- [x] Add tests to `tests/grid_tests.cpp` or a new `tests/highlight_tests.cpp`.
- [x] Build and run: `cmake --build build --target draxul-tests && ctest`.

---

## Acceptance

- All listed combinations pass.
- Edge cases (default fg/bg values, sentinel values for "not set") are explicitly exercised.

---

## Interdependencies

- No upstream blockers; self-contained.

---

*claude-sonnet-4-6*
