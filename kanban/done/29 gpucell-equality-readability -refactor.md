# GpuCell Equality Operator Readability Refactor

**Type:** refactor
**Priority:** 29
**Raised by:** Claude

## Summary

`GpuCell::operator==` is a single expression chaining approximately 25 member comparisons. This is unmaintainable: adding or removing a field requires manually editing the chain, and it is easy to miss a field. Replace with `= default` (if the struct layout allows it) or with a structured field-group comparison.

## Background

C++20 `= default` for `operator==` generates correct, efficient, field-by-field comparison automatically and stays correct as the struct evolves. If the struct contains types that do not support `= default` comparison (e.g., raw arrays), those fields can be handled with a small helper. Either approach is vastly more maintainable than a handwritten 25-expression chain.

## Implementation Plan

### Files to modify
- `libs/draxul-renderer/` — locate `GpuCell` definition (likely in a renderer-shared header); update `operator==`

### Steps
- [x] Locate `GpuCell` struct definition and its `operator==`
- [x] Check if all member types support `= default` comparison (all arithmetic types, `std::array`, `enum class` members do; raw C arrays do not)
- [x] Convert `uint32_t _pad[3]` to `std::array<uint32_t, 3>` to enable `= default` (same size and alignment — `sizeof(GpuCell)` remains 112 bytes)
- [x] Replace the free `operator==` with `bool operator==(const GpuCell&) const = default;` inside the struct
- [x] Update the test in `tests/renderer_state_tests.cpp` to reflect new semantics (padding is compared, not ignored; in real usage `_pad` is always zero)
- [x] Build and tests pass
- [x] Smoke test passes

## Depends On
- None

## Blocks
- None

## Notes
`_pad` is always zero-initialized in real usage (`GpuCell{}` is used everywhere), so `= default` comparison is semantically equivalent for production code. The old test was checking the implementation detail of the old ignore-padding behavior; the new test verifies that equality correctly tracks changes to meaningful fields.

> Work item produced by: claude-sonnet-4-6
