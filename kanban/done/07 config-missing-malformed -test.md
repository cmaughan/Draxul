# Test: Config Load Edge Cases (Missing, Malformed, Empty APPDATA)

**Type:** test
**Priority:** 7
**Source:** Claude review

## Problem

The config loading path (`libs/draxul-config/src/app_config_io.cpp`) has no tests for:
1. Missing config file → should apply defaults silently.
2. Malformed TOML → should log a clear error and apply defaults, not crash.
3. Empty `APPDATA` env var (Windows) → should use a fallback path, not write to CWD.
4. Unknown keys in TOML → should warn (already tracked in complete `02 config-unknown-key-warning -bug.md`; verify it still fires).

These are the most common failure modes for a first-time user or CI environment.

## Investigation steps

- [x] Read `app_config_io.cpp` fully — trace all load/save paths and error handling branches.
- [x] Check the TOML parser's error type — how are parse errors reported to the caller?
- [x] Read existing config tests in `tests/` to avoid duplication.

## Test design

Add to `tests/config_tests.cpp` (or create it).

### Missing file

- [x] Call `AppConfig::load()` with a path that does not exist.
- [x] Assert: returns successfully (or a defined "not found" result), all fields at their documented defaults.
- [x] Assert: no exception propagates, no crash.

### Malformed TOML

- [x] Write a temp file containing invalid TOML (e.g. `font_size = [unclosed`).
- [x] Call `AppConfig::load()` on it.
- [x] Assert: returns an error or applies defaults; logs a `WARN` or `ERROR`; does not crash.

### Empty APPDATA (Windows, compile-guarded)

- [x] On Windows, temporarily set `APPDATA=""` in the process environment before calling the path-construction function.
- [x] Assert: the resulting path is an absolute path (does not start with just the filename).
- [x] Assert: a `WARN` is logged.

### Unknown keys

- [x] Write a TOML file with an unrecognised key (`unknown_future_option = true`).
- [x] Assert: `WARN` is logged, remaining known keys are loaded correctly.

## Acceptance criteria

- [x] All tests pass on macOS and are gated appropriately for Windows-only paths.
- [x] No crashes or unhandled exceptions in any of the above scenarios.
- [x] Tests are part of `draxul-tests`.

## Interdependencies

- **`02 appdata-empty-string-config -bug`**: fix must land before the empty-APPDATA test passes.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
