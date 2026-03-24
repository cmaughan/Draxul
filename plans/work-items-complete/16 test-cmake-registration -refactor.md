# 16 test-cmake-registration -refactor

**Type:** refactor
**Priority:** 16
**Source:** GPT review (review-latest.gpt.md) — MEDIUM finding

## Problem

`tests/CMakeLists.txt` is a single hand-maintained list of every test `.cpp` file. It also adds private include roots from `app/` and `libs/draxul-window/src/` to the test target. This causes two problems:

1. **Merge conflict magnet.** Any two agents or developers adding test files at the same time will conflict on the same list in the same file.
2. **Tests couple to non-public internals.** The private include paths let tests `#include` headers that are not part of any library's public API. This creates invisible coupling — a refactor that moves an internal header breaks a test that should not have known about it.

Note: `21 test-registration-auto-discovery-refactor` is already COMPLETE, which may have partially addressed this. Verify before doing any work.

## Acceptance Criteria

- [x] Read `tests/CMakeLists.txt` in full. Check if `21 test-registration-auto-discovery-refactor` fully resolved the single-file list issue. — Work item 21 addressed Catch2 TEST_CASE auto-registration but did NOT touch the CMake source file list.
- [x] If the single-file list still exists: convert to `file(GLOB_RECURSE ...)` or a per-test-file `target_sources` approach so new test files are discovered automatically. — Converted to `file(GLOB ...)` matching `test_main.cpp` and `*_tests.cpp`.
- [x] Remove private include paths that expose non-public internals. If a test genuinely needs access to internals, the internals should be promoted to a testable public API or a test-support library. — Removed both `${CMAKE_SOURCE_DIR}/app` (redundant: `draxul-app` library already exports it via PUBLIC include) and `${CMAKE_SOURCE_DIR}/libs/draxul-window/src` (unused: no test includes any header from that path).
- [x] Confirm no test is relying on `app/` private headers for correctness — if so, refactor that test first. — Five tests include `"app.h"` but this is the public header of the `draxul-app` library, which is already on the include path via `target_link_libraries(draxul-tests PRIVATE draxul-app)`.
- [x] Build and run `ctest` to confirm all tests still pass. — 730 test cases, 690 passed, 40 skipped, 0 failed.

## Implementation Notes

- If `21 test-registration-auto-discovery-refactor` already fully solved this, mark this item complete immediately and note what it found.
- The private-include-path issue is the more important part even if auto-discovery is already done.

## Interdependencies

- Reduces merge conflicts for all future `-test` items.
- No blockers.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
