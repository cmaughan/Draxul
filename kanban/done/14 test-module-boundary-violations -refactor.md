# 14 test-module-boundary-violations -refactor

**Priority:** MEDIUM
**Type:** Refactor (enforce public API boundaries in test compilation)
**Raised by:** GPT-4o
**Model:** claude-sonnet-4-6

---

## Problem

`tests/CMakeLists.txt:67` compiles `app/*.cpp` files directly into the `draxul-tests` binary. `tests/clipboard_tests.cpp:6` includes a private header from a host module's `src/` directory. Both violations mean:

1. The "public API only" claim for the libraries is non-credible — tests can access and depend on private implementation details.
2. Parallel multi-agent edits to `app/*.cpp` and tests will collide at build time because both targets compile the same translation units.
3. Internal refactors of `app/` break tests silently even when the public API is unchanged.

---

## Code Locations

- `tests/CMakeLists.txt:67` — direct `app/*.cpp` inclusion
- `tests/clipboard_tests.cpp:6` — private `src/` header include
- `app/CMakeLists.txt` — where app sources are defined as a library or executable

---

## Implementation Plan

- [x] Read `tests/CMakeLists.txt` to understand which `app/*.cpp` files are compiled into the test binary and why (what do the tests need from them?).
- [x] Read `tests/clipboard_tests.cpp` to understand what the private header provides and whether the same interface is available via a public header.
- [x] For each `app/*.cpp` source compiled into tests:
  - If it contains genuinely testable logic that belongs in a library, move it to `draxul-app-support` (if appropriate) or a new thin `draxul-app-logic` library.
  - If it is only wiring code that tests should not need, remove it from the test link and use fakes/stubs instead.
- [x] For `clipboard_tests.cpp`: replace the private header include with the public `include/draxul/` equivalent. If no public header exposes the needed interface, that is evidence the interface should be promoted.
- [x] After the change, `draxul-tests` should link only against public library targets (`draxul-app-support`, `draxul-host`, `draxul-grid`, etc.) and not compile `app/*.cpp` directly.
- [x] Build: `cmake --build build --target draxul draxul-tests && py do.py smoke`
- [x] Run all tests: `ctest --test-dir build -R draxul-tests`
- [x] Run `clang-format`.

---

## Acceptance Criteria

- `tests/CMakeLists.txt` does not list any `app/*.cpp` source file directly.
- `tests/clipboard_tests.cpp` does not include any file from a module's `src/` directory.
- All existing tests pass after the change.
- No new private header includes are introduced.

---

## Interdependencies

- This refactor is a prerequisite for future multi-agent parallel work on `app/` — it removes the collision point.
- May require coordinating with whoever owns `draxul-app-support` (no current owner).
- A sub-agent is appropriate for this item since it touches CMake, tests, and potentially library structure simultaneously.

---

*claude-sonnet-4-6*
