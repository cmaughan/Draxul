# MpackValue Variant Type Stability Test

**Type:** test
**Priority:** 11
**Raised by:** Claude

## Summary

Add a test that statically pins the mapping between `MpackValue`'s variant alternative indices and the `MpackValueType` enum values, so that any reordering of the variant's template arguments produces a compile-time failure rather than a silent runtime mismatch.

## Background

Work item `02` addresses the root cause: `type()` uses `storage.index()` which is sensitive to variant declaration order. This test item provides a permanent guard. Even after the fix (which may use `std::holds_alternative<T>` checks instead), the test serves as documentation of the intended type-to-enum mapping and catches regressions if the fix is later reverted or incorrectly modified.

## Implementation Plan

### Files to modify
- `libs/draxul-nvim/tests/` (or equivalent) — add `mpack_value_type_test.cpp`
- `libs/draxul-nvim/CMakeLists.txt` — register test with ctest

### Steps
- [x] Write static assertions (using `static_assert` and `std::variant_alternative_t`) pinning each variant index to its expected C++ type — e.g., `static_assert(std::is_same_v<std::variant_alternative_t<0, MpackValue::Storage>, std::monostate>)`
- [x] Write runtime test: construct a `MpackValue` holding each supported type; verify `type()` returns the correct `MpackValueType` enum value for each
- [x] Write test: `MpackValue` holding a bool → `type()` returns `MpackValueType::Bool` (not Int or any other)
- [x] Write test: `MpackValue` holding int64 → `type()` returns `MpackValueType::Int`
- [x] Write test: `MpackValue` holding string → `type()` returns `MpackValueType::Str`
- [x] Cover all supported variant types
- [x] Register tests with ctest

## Depends On
- `02 mpackvalue-type-coupling -bug.md` — ideally the fix is in place first, but the test can be written independently

## Blocks
- None

## Notes
The static assertion approach is the most valuable part of this work item because it catches the problem at compile time with zero maintenance cost. The runtime assertions document expected behaviour.

> Work item produced by: claude-sonnet-4-6
