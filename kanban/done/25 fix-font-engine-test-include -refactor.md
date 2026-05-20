# 25 Fix font_engine.h Test Include Path Bypass

## Why This Exists

`tests/CMakeLists.txt` adds `libs/draxul-font/src` to the test include path, allowing
`tests/font_tests.cpp` to directly include the private header `font_engine.h`:

```cpp
// tests/font_tests.cpp
#include "font_engine.h"
```

`font_engine.h` is an implementation-private header. It is not part of `draxul-font`'s public API.
Tests that depend on it are coupled to internal implementation details: any refactoring of
`font_engine.h` that does not change the public `TextService` API will silently break the test build.

**Source:** `tests/CMakeLists.txt` (the offending include path), `tests/font_tests.cpp`.
**Raised by:** Claude.

## Goal

Remove the `libs/draxul-font/src` include path from the test CMake target. Rewrite any tests
that used `font_engine.h` to go through the public `TextService` API instead. If certain internal
behaviours are not currently testable via the public API, decide whether to add a narrow test seam
to `TextService` or remove the test.

## Implementation Plan

- [x] Read `tests/CMakeLists.txt` to find the offending `target_include_directories` line.
- [x] Read `tests/font_tests.cpp` to understand which tests use `font_engine.h` and what they test.
- [x] For each test using internal headers:
  - If the behaviour is testable via `TextService` (shape text, get glyph info), rewrite using the public API.
  - If the behaviour is not exposed publicly but is worth testing, add a narrow accessor or factory to `TextService` — document it as test-only.
  - If the test covers behaviour that is adequately covered by higher-level tests, delete it.
- [x] Remove the `libs/draxul-font/src` include path from `tests/CMakeLists.txt`.
- [x] Confirm the tests build and pass without the private include path.
- [x] Run `clang-format` on any touched test files.
- [x] Run `ctest --test-dir build`.

## Completion Notes

- Removed the private `libs/draxul-font/src` include path from the `draxul-tests` target.
- Reworked `tests/font_tests.cpp` so it no longer includes `font_engine.h`.
- Replaced the private-header-based cache-growth test with a public `TextService` test that exercises the same behavior through `resolve_cluster()` and the new bounded-cache seam.

## Sub-Agent Split

Single agent. Changes are confined to `tests/font_tests.cpp` and `tests/CMakeLists.txt`.
