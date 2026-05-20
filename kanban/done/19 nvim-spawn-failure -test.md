# 19 NvimProcess Spawn Failure Test

## Why This Exists

`NvimProcess::spawn()` launches `nvim --embed` via `CreateProcess` (Windows) or `fork()`/`exec()` (macOS). If the path to `nvim` is wrong or the binary is not found, `spawn()` should return `false` and log an error. This error path is never exercised by the existing test suite.

If the error handling is wrong (e.g., process handle leak, log message missing, wrong return value), it fails silently.

**Source:** `libs/draxul-nvim/src/nvim_process.cpp` (platform-specific spawn logic).
**Raised by:** Claude.

## Goal

Add a test that:
1. Calls `NvimProcess::spawn("nonexistent-nvim-binary-path")`.
2. Verifies `spawn()` returns `false`.
3. Verifies an error was logged (via the logging infrastructure or a captured log sink).
4. Verifies no file handles or process handles are leaked (no resource ownership after failure).

## Implementation Plan

- [x] Read `libs/draxul-nvim/src/nvim_process.cpp` and `libs/draxul-nvim/include/draxul/nvim.h` to understand the `NvimProcess::spawn()` API and logging.
- [x] Check if there is a way to redirect log output to a test sink (e.g., a capturable `ILogger` or `std::ostringstream`). If not, add a minimal log capture seam.
- [x] Write test:
  - `nvim_spawn_returns_false_for_bad_path`
  - `nvim_spawn_logs_error_for_bad_path`
- [x] Add tests to `draxul-tests` (new file `tests/nvim_process_tests.cpp` or extend existing).
- [x] Run `ctest --test-dir build`.

## Notes

This test is safe to run in CI because the "bad path" binary will simply not exist.
Do not hardcode a real `nvim` path — use a guaranteed-nonexistent path like `/tmp/nonexistent-nvim`.
On Windows, use a path like `C:\nonexistent\nvim.exe`.

Implemented in `tests/nvim_process_tests.cpp`. The Windows failure path also now closes the `NUL`
stderr handle before returning so the bad-path case does not leak that handle.

## Sub-Agent Split

Single agent. Platform-specific: the test will need `#ifdef`s or will be written twice
(once in a Windows-specific test source, once in a macOS-specific test source) if process spawning
behaviour differs substantially.
