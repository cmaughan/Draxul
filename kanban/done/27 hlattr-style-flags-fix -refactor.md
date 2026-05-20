# HlAttr Style Flags Magic Literals Fix

**Type:** refactor
**Priority:** 27
**Raised by:** Claude

## Summary

`HlAttr::style_flags()` in `libs/draxul-types/include/draxul/types.h` uses raw integer literals (1, 2, 4, 8, 16) to construct the style bitmask instead of named `STYLE_FLAG_*` constants. Replace the literals with named constants to eliminate the silent-divergence risk if flag assignments change.

## Background

Magic numbers in bitmask operations are a common source of subtle bugs. If anyone adds a new style flag and inserts it at position 2 (shifting existing flags), or if the bitmask semantics are referenced elsewhere in the codebase with inconsistent values, the only way to catch the divergence is a careful manual audit. Named constants with `static_assert` guards make mismatches a compile-time error. This is a small, safe, high-value refactor.

## Implementation Plan

### Files to modify
- `libs/draxul-types/include/draxul/types.h` — add `STYLE_FLAG_BOLD`, `STYLE_FLAG_ITALIC`, `STYLE_FLAG_UNDERLINE`, `STYLE_FLAG_UNDERCURL`, `STYLE_FLAG_STRIKETHROUGH` (and any others) as `constexpr uint32_t` constants; update `style_flags()` to use them

### Steps
- [x] Read `HlAttr` definition and `style_flags()` implementation to identify all flag bits used
- [x] Define named constants: `constexpr uint32_t STYLE_FLAG_BOLD = 1;` etc. (or as an `enum class StyleFlag : uint32_t` with individual values)
- [x] Replace every raw literal in `style_flags()` with the corresponding named constant
- [x] Search the codebase for any other use of these raw bit values (e.g., in shader constants, renderer code) and update those to use the constants as well
- [x] Run `ctest` to confirm no regressions

## Completion Notes

- `HlAttr::style_flags()` in `libs/draxul-types/include/draxul/highlight.h` updated to use `STYLE_FLAG_BOLD`, `STYLE_FLAG_ITALIC`, `STYLE_FLAG_UNDERLINE`, `STYLE_FLAG_STRIKETHROUGH`, `STYLE_FLAG_UNDERCURL` instead of raw literals 1, 2, 4, 8, 16.
- `shaders/grid_bg.frag` (GLSL): added named `const uint STYLE_FLAG_*` constants with comments referencing `draxul/types.h`; replaced 4u, 8u, 16u literals.
- `shaders/grid_fg.frag` (GLSL): added `STYLE_FLAG_COLOR_GLYPH = 32u`; replaced `(1u << 5)` literal.
- `shaders/grid.metal` (Metal): added named `constant uint STYLE_FLAG_*` constants; replaced all raw literals.
- Test file comment in `tests/hlattr_style_flags_tests.cpp` updated to reflect that the refactor has landed.
- Build, tests, and smoke test all pass.

## Depends On
- None

## Blocks
- `10 hlattr-style-flags-constants -test.md` — the test verifies this refactor

## Notes
If an `enum class StyleFlag` is used instead of plain constants, ensure the bitmask operations (`|`, `&`) work correctly. A plain `uint32_t` or a bitmask enum helper may be preferable to an unadorned `enum class` for bitfield use.

> Work item produced by: claude-sonnet-4-6
