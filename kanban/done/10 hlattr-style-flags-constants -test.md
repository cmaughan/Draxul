# HlAttr Style Flags Constants Test

**Type:** test
**Priority:** 10
**Raised by:** Claude

## Summary

Add a compile-time or unit test that verifies `HlAttr::style_flags()` returns values consistent with the `STYLE_FLAG_*` named constants, ensuring that any future change to flag bit assignments is caught immediately rather than silently diverging.

## Background

Work item `27` replaces the magic literals (1, 2, 4, 8, 16) in `style_flags()` with named `STYLE_FLAG_*` constants. This test item provides the safety net that catches any future divergence between the constants and the method. The test is intentionally simple: it just asserts the bit values of each flag constant and verifies that `style_flags()` produces the expected bit when each attribute is set.

## Implementation Plan

### Files to modify
- `libs/draxul-types/tests/` (or equivalent) — add `hlattr_style_flags_test.cpp`
- `libs/draxul-types/CMakeLists.txt` — register test with ctest

### Steps
- [x] Write static assertions (or runtime assertions) pinning each `STYLE_FLAG_*` constant to its expected bit value: BOLD=1, ITALIC=2, UNDERLINE=4, UNDERCURL=8, STRIKETHROUGH=16 (or whatever the actual mapping is)
- [x] Write test: construct `HlAttr` with bold=true, verify `style_flags()` returns a value with bit 0 set (or the BOLD flag bit)
- [x] Write test: construct `HlAttr` with all style flags set, verify `style_flags()` returns all expected bits
- [x] Write test: construct `HlAttr` with no style flags, verify `style_flags()` returns 0
- [x] Write test: verify that each flag constant is a distinct power of two (no overlap)
- [x] Register tests with ctest

## Depends On
- `27 hlattr-style-flags-fix -refactor.md` — the refactor should land first so the test verifies named constants rather than literals

## Blocks
- None

## Notes
If the `STYLE_FLAG_*` constants do not yet exist (i.e., work item `27` has not been done), the test can be written against the expected values and will serve as a forcing function for the refactor.

> Work item produced by: claude-sonnet-4-6
