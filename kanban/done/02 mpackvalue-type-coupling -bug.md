# MpackValue::type() Variant-Index Coupling

**Type:** bug
**Priority:** 02
**Raised by:** Claude

## Summary

`MpackValue::type()` returns an enum value by switching on `storage.index()` — the position of the active type in the `std::variant` declaration. If the variant's template arguments are ever reordered (e.g., during a refactor), the enum values silently map to the wrong types, causing corrupt message parsing with no compile-time error.

## Background

`std::variant::index()` returns a `size_t` representing which alternative is currently active, numbered from 0 in declaration order. When `type()` switches on this index to return a `MpackValueType` enum, it implicitly assumes that the variant's declaration order exactly matches the enum's declaration order. This is a fragile coupling: there is no `static_assert` or compiler check enforcing the alignment. A future maintainer adding a new type to the variant or reordering for logical grouping will introduce a silent runtime bug where, for example, a boolean value reports as an integer or vice versa.

## Implementation Plan

### Files to modify
- `libs/draxul-nvim/src/` — locate the file containing `MpackValue` and its `type()` method (likely `mpack_value.h` or similar)
- Replace the index-based switch with `std::holds_alternative<T>` checks, OR add `static_assert` guards pinning each index to its expected enum value

### Steps
- [x] Locate the `MpackValue` definition and `type()` implementation
- [x] Option A (preferred): Replace the switch on `storage.index()` with a chain of `std::holds_alternative<T>` checks, one per supported type; the enum value returned is independent of declaration order
- [x] Option B (acceptable if A is impractical): N/A — Option A (holds_alternative) was implemented
- [x] Add a comment explaining why declaration order must match the enum order (N/A — Option A chosen, no ordering dependency)
- [x] Verify the change does not affect runtime behaviour for all variant types

## Depends On
- None

## Blocks
- `11 mpackvalue-variant-stability -test.md` — the test should pin variant ordering once the fix establishes the correct approach

## Notes
Option A is cleanly safe with zero ordering assumptions. Option B is a guard rather than a fix but may be preferable if the switch needs to remain for performance reasons. Either way, the test in work item `11` should serve as a regression check.

> Work item produced by: claude-sonnet-4-6
