# 33 cli-malformed-args -test

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.gpt.md [P]; regression guard for WI 26*

## Problem

After fixing WI 26 (`cli-numeric-arg-crash -bug`), there are no automated tests to prevent
future regressions in CLI argument parsing.  Other flags that accept numeric input
(`--log-level`, `--screenshot-count`, any future numeric flag) could introduce the same
unchecked-`stoi` pattern.

This test item is the regression guard for WI 26 and provides a template for testing future
CLI argument validation.

## Acceptance Criteria

- [ ] WI 26 is implemented first.
- [ ] A test (or test binary) invokes the CLI arg parser (or the relevant parse function) with
      `--screenshot-delay abc` and verifies the process exits with a non-zero code and a
      helpful message on stderr.
- [ ] Same for `--screenshot-delay -1`, `--screenshot-size 0`, `--screenshot-size xyz`.
- [ ] A test verifies that valid values (`--screenshot-delay 100`, `--screenshot-size 1024`)
      parse correctly.
- [ ] All tests pass under `ctest`.

## Implementation Plan

1. Determine if `parse_args()` in `app/main.cpp` can be extracted to a testable function
   that does not call `SDL_Init` or launch nvim.  If not, test via subprocess invocation
   (launch the binary with bad flags and check exit code + stderr).
2. Write 4 invalid-arg tests and 2 valid-arg tests.
3. If subprocess approach: use `std::system` / `popen` in the test or a CMake `add_test`
   command-line test.
4. Run `ctest -R cli`.

## Files Likely Touched

- `tests/cli_args_tests.cpp` (new, or add to existing smoke tests)
- `app/main.cpp` — may need a small refactor to extract `parse_args()` as a testable function
- `tests/CMakeLists.txt`

## Interdependencies

- **Prerequisite: WI 26** (`cli-numeric-arg-crash -bug`) must be fixed first.
- **WI 41** (`cmake-configure-depends`) should land first if creating a new test file.
