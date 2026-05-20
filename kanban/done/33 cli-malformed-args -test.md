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

- [x] WI 26 is implemented first.
- [x] A test invokes the CLI arg parser with `--screenshot-delay abc` and verifies that
      `parse_args()` returns an error result with a helpful message (rather than calling
      `std::exit`).
- [x] Same for `--screenshot-delay -1`, `--screenshot-size 0x600`, `--screenshot-size xyz`,
      `--screenshot-delay 100x` (trailing-garbage), `--screenshot-size abcx600`.
- [x] Tests verify that valid values (`--screenshot-delay 100`, `--screenshot-delay 0`,
      `--screenshot-size 1024x768`) parse correctly.
- [x] All tests pass under `ctest`.

## Implementation

- [x] Extracted `parse_args()` and `ParsedArgs` from `app/main.cpp` into new
  `app/cli_args.{h,cpp}` files in the `draxul-app` library, so the parser is
  reachable from `tests/`.
- [x] Refactored `parse_args()` to return `ParseArgsResult { ParsedArgs args; std::optional<std::string> error; }`
  rather than calling `std::fprintf(stderr,…)` + `std::exit(1)`. `main.cpp` now
  prints the message and returns 1 itself.
- [x] Tightened numeric parsing: `--screenshot-delay 100x` and `--screenshot-size abcx600`
  are now rejected (previously `std::stoi` silently truncated trailing garbage).
- [x] Added `tests/cli_args_tests.cpp` with 11 test cases (8 invalid-input cases, 3 valid).

## Files Touched

- `app/cli_args.h` (new)
- `app/cli_args.cpp` (new)
- `app/main.cpp` — strip the inline parser, call `draxul::parse_args()`
- `CMakeLists.txt` — add `app/cli_args.cpp` to `draxul-app`
- `tests/cli_args_tests.cpp` (new)

## Interdependencies

- **Prerequisite: WI 26** (`cli-numeric-arg-crash -bug`) must be fixed first.
- **WI 41** (`cmake-configure-depends`) should land first if creating a new test file.
