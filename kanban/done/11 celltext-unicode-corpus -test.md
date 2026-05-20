# 11 celltext-unicode-corpus -test

**Type:** test
**Priority:** 11
**Source:** Claude review (truncation silent corruption); Gemini (unicode edge-case corpus)

## Problem

Previous truncation bugs (`00 celltext-silent-truncation-bug`, `02 celltext-silent-truncation -bug`) are marked complete, but both Claude and Gemini flag that complex emoji clusters (family emoji with skin tone modifiers, ZWJ sequences) can exceed 32 bytes and that silent truncation is still the failure mode for these. There is no regression corpus test to verify that:
1. Known multi-byte clusters are handled correctly (or truncated safely without memory corruption).
2. Adjacent cell memory is not corrupted when a cluster exceeds `kMaxLen`.
3. HarfBuzz shaping of ZWJ sequences does not crash or produce out-of-bounds glyph IDs.

## Acceptance Criteria

- [x] Read `libs/draxul-grid/include/draxul/grid.h` (`CellText::assign()`) and the existing grid tests.
- [x] Build a corpus of at least 5 known-difficult sequences:
  - Family emoji with skin tone: `👨‍👩‍👧‍👦` (likely > 32 bytes as UTF-8)
  - ZWJ sequence: `👁️‍🗨️`
  - Long combining sequence (combining characters stacked)
  - RTL mark sequences
  - A sequence that is exactly 32 bytes (boundary case)
- [x] For each sequence, add a test that:
  - [x] Calls `CellText::assign()` with the sequence.
  - [x] Asserts `len` matches the valid UTF-8 prefix length (not raw byte count).
  - [x] Verifies adjacent cell memory is unmodified (check with a canary value).
  - [x] Asserts no crash under `mac-asan`.
- [ ] Run `ctest` with `mac-asan` preset.

## Implementation Notes

- Unicode string literals in C++20 can be written as `u8"..."` — check the compiler's handling.
- If the 32-byte cap is raised as a result of this item's findings, that is acceptable as a scope expansion.
- A sub-agent is appropriate: read the CellText code and write the corpus test file.

## Interdependencies

- No blockers. Can be done independently.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
