# 07 attr-id-cache-correctness — Test

## Summary

`TerminalHostBase::attr_id()` maintains an `attr_cache_` that maps `HlAttr` structs to stable `uint16_t` IDs. It deduplicates attributes so that identical highlight combinations always produce the same ID. No tests currently verify:

1. That identical inputs return the same ID (deduplication).
2. That distinct inputs return distinct IDs (no false collisions).
3. That the cache does not grow with repeated duplicate inputs.

**This test item is a prerequisite companion for item 13 (attr-id-unordered-map refactor).** Write these tests first — they guard both the current O(n) implementation and the future O(1) replacement.

## Steps

- [x] 1. Read `libs/draxul-host/src/terminal_host_base.cpp` to find `attr_id()` implementation and `attr_cache_` type.
- [x] 2. Read `libs/draxul-host/include/draxul/` (or `libs/draxul-types/include/draxul/types.h`) to find the `HlAttr` struct definition and all its fields.
- [x] 3. Read `tests/terminal_vt_tests.cpp` or equivalent to understand how `TerminalHostBase` is instantiated in tests.
- [x] 4. Read `tests/CMakeLists.txt` and `tests/test_main.cpp` for registration patterns.
- [x] 5. Create `tests/attr_id_tests.cpp` (or add to an existing terminal test file if it is lightly populated).

  **Test 1: Same HlAttr returns same ID**
  - Construct two identical `HlAttr` objects (same foreground, background, flags).
  - Call `attr_id(a1)` and `attr_id(a2)`.
  - Assert: both return the same `uint16_t` ID.

  **Test 2: Different HlAttr returns different IDs**
  - Construct 5 distinct `HlAttr` objects (vary at least one field each time).
  - Call `attr_id()` for each.
  - Collect results into a set.
  - Assert: set has exactly 5 entries (no two share an ID).

  **Test 3: Cache does not grow with duplicate inputs**
  - Call `attr_id()` 100 times with the same `HlAttr`.
  - Assert: the cache size (or ID counter) has only incremented by 1, not 100.
  - (If cache size is not directly observable, verify by calling `attr_id()` with a fresh distinct attribute afterward and asserting its ID is 2, not 101.)

  **Test 4: Large number of distinct attributes**
  - Construct 100 distinct `HlAttr` objects (e.g., foreground color 0..99 with all other fields constant).
  - Call `attr_id()` for each.
  - Assert: 100 distinct IDs are returned.
  - Assert: calling the same 100 attributes again returns the same 100 IDs (stable mapping).

  **Test 5: Edge case — default/zero HlAttr**
  - Call `attr_id(HlAttr{})` (zero-initialized).
  - Assert: returns a valid ID (no crash, no ID=UINT16_MAX unless that is the sentinel value).
  - Call it a second time; assert same ID.

- [x] 6. Register the new test file in `tests/CMakeLists.txt`.
- [x] 7. Register test cases in `tests/test_main.cpp` if manual registration is used.
- [x] 8. Build: `cmake --build build --target draxul-tests`. Confirm no compile errors.
- [x] 9. Run: `ctest --test-dir build -R draxul-tests`. All tests must pass.
- [x] 10. Run clang-format on all new/touched files.
- [x] 11. Mark this work item complete and move to `plans/work-items-complete/`. (These same tests will be run again after item 13 to validate the O(1) replacement.)

## Acceptance Criteria

- Tests verify deduplication (same input → same ID), uniqueness (different inputs → different IDs), and non-growth of the cache for duplicates.
- A large-attribute test (100 distinct attributes) is present.
- All tests pass against the current O(n) implementation.
- All existing tests continue to pass.

*Authored by: claude-sonnet-4-6*
