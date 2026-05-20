# 41 cmake-configure-depends -refactor

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.gpt.md [P]*

## Problem

`tests/CMakeLists.txt` uses:

```cmake
file(GLOB TEST_SOURCES "*_tests.cpp")
```

Without `CONFIGURE_DEPENDS`, CMake will not re-run when a new `*_tests.cpp` file is added to
the directory.  The new test file will be silently excluded from the test binary until someone
manually runs `cmake --preset ...` to re-configure.

In a multi-agent workflow where agents regularly add new test files, this means newly written
tests can sit unexecuted without any warning — the exact failure mode that makes test
coverage gaps hard to detect.

## Acceptance Criteria

- [x] The `file(GLOB ...)` in `tests/CMakeLists.txt` is changed to:
      `file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "*_tests.cpp")`.
- [x] Equivalent glob calls in subdirectories (if any) are also updated.
- [x] A comment explains why `CONFIGURE_DEPENDS` is required.
- [x] The build still works: `cmake --build build --target draxul-tests` succeeds.

## Implementation Plan

1. Read `tests/CMakeLists.txt` to find all `file(GLOB ...)` calls.
2. Add `CONFIGURE_DEPENDS` to each one.
3. Add a comment: `# CONFIGURE_DEPENDS: re-run CMake when new *_tests.cpp files are added`.
4. Re-configure (`cmake --preset mac-debug` or equivalent) and build `draxul-tests`.
5. Verify all existing tests are discovered and pass.

## Files Likely Touched

- `tests/CMakeLists.txt`

## Interdependencies

- **This is the highest-priority trivial fix** — it should land before any WI that adds a new
  test file (WI 29, 30, 31, 32, 33, 34, 35).
- No other dependencies.
- **Sub-agent recommendation**: combine with WI 36 (`param-or-lambda-dedup`) in one agent
  pass — both are mechanical and low-blast-radius.
