# 09 celltext-truncation-tests -test

**Priority:** MEDIUM
**Type:** Test (documents known correctness hole)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`CellText` uses 32-byte inline storage (`kMaxLen = 32`) for grapheme clusters. Zero-width joiner emoji sequences (e.g., family emoji: 👨‍👩‍👧‍👦) can exceed 32 bytes. When truncation occurs, a warning is logged but the resulting glyph is silently corrupted. There are no tests that verify:
1. The truncation boundary is clean (does not split a UTF-8 multi-byte sequence mid-codepoint).
2. The warning is emitted.
3. The stored bytes are a valid UTF-8 prefix.

---

## Implementation Plan

- [x] Search for `CellText` and `kMaxLen` in the source tree to find the definition and the truncation site.
- [x] Read the `CellText` class and its string storage to understand the exact truncation behavior.
- [x] Write test cases:
  - [x] **Exact boundary** — a grapheme cluster of exactly 32 bytes stores successfully; no warning; `CellText::str()` (or equivalent) returns all 32 bytes.
  - [x] **One byte over** — 33 bytes; truncation occurs; warning is emitted; the stored string is valid UTF-8 (does not end mid-sequence).
  - [x] **ZWJ emoji (>32 bytes)** — construct a real 4-person family emoji sequence; store it; verify the result is a valid UTF-8 prefix of the original, not a mid-sequence truncation.
  - [x] **ASCII (well under limit)** — baseline, stores unchanged.
- [x] Hook into the log output to capture warnings (or use a test-aware log sink if one exists).
- [x] Add tests to an appropriate test file (check `tests/` for any cell or text-related test files).
- [x] Build and run: `cmake --build build --target draxul-tests && ctest`.

---

## Acceptance

- Tests pass and document the current behavior (even if the behavior is "truncation with warning").
- If a future fix extends `CellText` to use `std::string`, these tests will be the first to verify the fix.

---

## Note

The underlying correctness hole (truncation) is a known limitation. This work item is about *documenting and guarding* the boundary, not fixing it. A fix (switching to `std::string` or a larger buffer) would be a separate work item if prioritized.

---

## Interdependencies

- No upstream blockers; self-contained.

---

*claude-sonnet-4-6*
