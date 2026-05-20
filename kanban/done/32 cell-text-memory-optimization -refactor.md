# Cell.text Memory Optimization

**Type:** refactor
**Priority:** 32
**Raised by:** Claude

## Summary

`Cell.text` in `libs/draxul-grid/` is a `std::string`, which may heap-allocate for each cell containing text. On a 200×50 grid, this is up to 10,000 small heap objects active simultaneously. Replace with an inline `std::array<char, 16>` plus a length byte (or a `SmallString<16>` type) to eliminate the heap allocations for the common case of short UTF-8 sequences.

## Background

Most terminal characters are 1–4 bytes of UTF-8. Modern compilers implement small-string optimisation (SSO) in `std::string`, which avoids heap allocation for strings up to ~15 bytes. However, SSO is not guaranteed by the standard, and the overhead of `std::string` (24 bytes on 64-bit with capacity tracking) may still be larger than necessary for this use case. An explicit `std::array<char, 16>` + length avoids the capacity field and the SSO implementation variations across standard libraries. On a 200×50 grid, this can save 400–800 KB of metadata and eliminate 10,000 small allocations from the heap allocator's free lists.

## Implementation Plan

### Files to modify
- `libs/draxul-grid/include/draxul/grid.h` — replace `std::string text` in `Cell` with an inline text representation
- `libs/draxul-grid/src/grid.cpp` — update all code that reads/writes `cell.text` as a `std::string`
- Any other file that accesses `Cell::text` — update to use the new API

### Steps
- [x] Define a `CellText` type: `struct CellText { std::array<char, 16> data{}; uint8_t len = 0; }` with convenience methods `assign(const char*, size_t)`, `view() → std::string_view`, `empty() → bool`, `operator==`
- [x] Replace `std::string text` in `Cell` with `CellText text`
- [x] Update `Grid` methods that write to `cell.text` (character assignment from RPC, blank-fill on scroll/clear)
- [x] Update all read paths (text shaping input to HarfBuzz, equality checks)
- [x] Measure `sizeof(Cell)` before and after to confirm the size change is favourable
- [x] Run `ctest` and smoke tests

## Depends On
- None

## Blocks
- None

## Notes
16 bytes is sufficient for all Unicode characters representable as a single cell (the longest valid UTF-8 sequence is 4 bytes; cell text including combining characters may be longer but rarely exceeds 12 bytes). A `len > 16` case should be handled gracefully (truncate or assert) rather than silently corrupting. Verify that the `sizeof(Cell)` after the change is aligned favourably for the grid array layout.

> Work item produced by: claude-sonnet-4-6
