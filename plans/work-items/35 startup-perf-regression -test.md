# 35 startup-perf-regression -test

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.gemini.md [G]*

## Problem

`App::initialize()` already records per-step startup timings that appear in the diagnostics
panel, but there is no CI-gated test that fails if total startup time exceeds a budget.
Startup regressions (e.g., a new synchronous font atlas pre-warm, a slow config read, an
expensive plugin scan at startup) are only caught after users report them.

## Acceptance Criteria

- [ ] A test (or CMake `add_test`) runs the Draxul smoke binary in headless mode and reads the
      startup timing from stdout/log output.
- [ ] The test fails if total `App::initialize()` time exceeds a configurable threshold
      (suggested: 500 ms on a developer machine; parameterisable via environment variable or
      CMake option to avoid false positives on slow CI runners).
- [ ] The test is documented as a canary, not a hard gate — CI failure triggers investigation,
      not an automatic block.
- [ ] Test passes on both macOS and Windows CI.

## Implementation Plan

1. Confirm that the existing smoke binary (`draxul --smoke` or equivalent) already emits
   startup timing lines to stdout or a log file.
2. If not, add a `--print-startup-timing` flag (or extend `--log-level info` output) to emit
   a machine-readable timing line: `STARTUP_MS: 123`.
3. Write a Python or CMake-based test that launches the binary, captures output, extracts
   `STARTUP_MS`, and exits non-zero if it exceeds the threshold.
4. Wire it as a `ctest` test with a label `perf` so it can be skipped with `-LE perf` on
   slow CI runners.
5. Document the threshold rationale in the test file.

## Files Likely Touched

- `app/app.cpp` — may need a minor logging addition
- `tests/startup_perf_test.py` or `tests/CMakeLists.txt` add_test() entry
- `do.py` — add a `py do.py perfgate` command for local use

## Interdependencies

- Independent of other open WIs.
- Should not be merged until the diagnostics panel timing infrastructure is stable.
