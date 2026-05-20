# ASan / LSan CI Integration

**Type:** test
**Priority:** 20
**Raised by:** Gemini

## Summary

Configure AddressSanitizer (ASan) and LeakSanitizer (LSan) for the CMake test builds on CI so that memory errors and leaks in unit tests are caught automatically on every pull request.

## Background

Several bugs identified in this review (grid scroll OOB, selection truncation, scrollback eviction) involve potential memory safety violations. ASan detects these at runtime with minimal false-positive rate. LSan catches memory leaks that accumulate over test runs. Neither is currently enabled in CI. Adding them to the test build configuration ensures that future PRs touching memory-sensitive code are automatically validated.

## Implementation Plan

### Files to modify
- `CMakeLists.txt` or `cmake/` — add an optional CMake preset or a build flag (`ENABLE_SANITIZERS`) that appends `-fsanitize=address,leak` (or `/fsanitize:address` on MSVC) to compile and link flags for test targets
- `.github/workflows/` or equivalent CI config — add a CI job that builds with sanitizers enabled and runs `ctest`

### Steps
- [x] Add a CMake option `DRAXUL_ENABLE_SANITIZERS` (default OFF)
- [x] When the option is ON, add `-fsanitize=address -fno-omit-frame-pointer` to compile and link flags for all draxul targets (not FetchContent deps). On macOS/Apple Clang `-fsanitize=address` is used (LSan is bundled inside ASan); on Linux `-fsanitize=address,leak` is used; on MSVC `/fsanitize:address` is used.
- [x] Handle macOS: ASan is available via Clang; LSan is part of ASan on macOS (Apple Clang does not support `-fsanitize=leak` standalone)
- [x] Handle Windows: MSVC ASan (`/fsanitize:address`) does not support LSan; enable only ASan on Windows CI
- [x] Add a `mac-asan` CMake preset inheriting from `mac-debug` with `-DDRAXUL_ENABLE_SANITIZERS=ON`
- [x] Add a CI job `asan-macos` in `.github/workflows/build.yml` with `continue-on-error: true` that configures with `mac-asan`, builds `draxul-tests`, and runs `ctest -R draxul-tests`
- [x] Document the `mac-asan` preset in `CLAUDE.md` build commands section
- [ ] Add a CI job for Windows analogously (deferred — MSVC ASan has restrictions in CI environments)
- [ ] Run existing tests under ASan; fix any pre-existing violations before enabling the CI gate (gate is `continue-on-error: true` for now)

## Depends On
- `08 grid-scroll-bounds -test.md` — should run under ASan
- `15 selection-truncation-boundary -test.md` — should run under ASan
- `14 scrollback-overflow-eviction -test.md` — should run under ASan

## Blocks
- None

## Notes
Start by running ASan locally on the existing test suite before adding the CI gate; there may be pre-existing suppressible issues (e.g., in third-party libraries fetched via FetchContent). Use ASan suppression files for known third-party issues rather than disabling ASan for entire test binaries.

> Work item produced by: claude-sonnet-4-6
