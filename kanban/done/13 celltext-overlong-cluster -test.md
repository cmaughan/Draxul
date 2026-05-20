# 13 celltext-overlong-cluster -test

**Priority:** MEDIUM
**Type:** Test (safety guard for CellText truncation of complex Unicode)
**Raised by:** Claude + GPT-4o (both flagged)
**Model:** claude-sonnet-4-6

---

## Problem

`CellText::assign()` in `grid.h` has a hard cap of `kMaxLen = 32` bytes. Modern emoji ZWJ sequences (e.g., a family emoji `👨‍👩‍👧‍👦`) can exceed 32 bytes. The truncation produces a malformed UTF-8 cluster that the HarfBuzz shaper will either reject or render as replacement characters. There is no test that drives this path, so truncation is silent and its downstream effects (crash? garble? warning log?) are unknown.

The `TODO` comment at `grid.h:15` has existed since initial commit without resolution.

---

## Code Locations

- `libs/draxul-grid/include/draxul/grid.h` — `CellText`, `kMaxLen`, `assign()`
- `libs/draxul-font/` — HarfBuzz shaping path that receives `CellText` bytes
- `tests/terminal_vt_tests.cpp` or a new `grid_tests.cpp` — where to add the test

---

## Implementation Plan

- [x] Read `grid.h` `CellText::assign()` to see exactly what happens on truncation (is there a log call? does it assert?).
- [x] Find where `CellText` bytes are handed to HarfBuzz (look in `TextService` or `GlyphCache`). Understand what happens if the input is not valid UTF-8.
- [x] Construct a 33-byte ZWJ family emoji sequence in a test. Good candidate: `"\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6"` (👨‍👩‍👧‍👦, 25 bytes but demonstrates the class). Use a longer synthetic sequence if needed to exceed 32 bytes.
- [x] Test 1: assert that `CellText::assign()` with a 33-byte input does NOT crash.
- [x] Test 2: assert that a `WARN` or `DEBUG` log is emitted (if the implementation promises this).
- [x] Test 3: assert that the stored bytes are valid UTF-8 after truncation (truncation must not break in the middle of a multi-byte codepoint). Even if the cluster is semantically incomplete, the bytes passed to the shaper should be well-formed.
- [x] If the truncation currently breaks UTF-8 validity, fix `CellText::assign()` to truncate at a valid UTF-8 codepoint boundary.
- [x] Build and run tests.
- [x] Run `clang-format`.

---

## Acceptance Criteria

- `CellText::assign()` with input > 32 bytes does not crash.
- The stored bytes are valid UTF-8 (no broken multi-byte sequences at the boundary).
- A warning log fires at `WARN` or `DEBUG` level.
- The shaper does not receive invalid UTF-8.
- The `TODO` in `grid.h` is updated to reflect either the continued limitation or the fix.

---

## Interdependencies

- No upstream blockers. The 32-byte cap is a known limitation; this test documents its failure mode and the truncation is a partial fix.
- A future work item could remove the cap with a heap fallback; that is not in scope here.

---

*claude-sonnet-4-6*
