# 02 Config Unknown Key Warning

## Why This Exists

TOML config typos (e.g. `font_szie = 14`) are silently discarded. This makes it hard for users
to diagnose misconfigured options. Adding a warning log for unknown keys surfaces the problem
immediately.

**Source:** `libs/draxul-app-support/src/app_config.cpp`
**Raised by:** task specification

## Goal

- [x] After parsing all known keys from the config table, iterate remaining keys in the parsed
  table and emit `DRAXUL_LOG_WARN` for each unknown key, e.g.:
  `[config] Unknown key 'font_szie' — check spelling`
- [x] For type mismatches (wrong value type for a known key), log an error and use the default
  value, e.g.: `[config] Key 'font_size' has wrong type (expected integer) — using default`
- [x] Add tests covering the unknown-key warning, the no-warning-for-valid-config case, and the
  type-mismatch error log.

## Implementation Plan

- [x] Read `libs/draxul-app-support/src/app_config.cpp` fully.
- [x] Add `kKnownTopLevelKeys` array with all 8 known top-level keys.
- [x] Add type-check lambdas (`check_int_type`, `check_bool_type`, `check_string_type`,
  `check_array_type`) that call `DRAXUL_LOG_ERROR` when a known key is present with the wrong type.
- [x] After all known-key parsing, iterate the document and call `DRAXUL_LOG_WARN` for every key
  not in `kKnownTopLevelKeys`.
- [x] Add three new tests to `tests/app_config_tests.cpp`:
  - `app config parse warns about unknown top-level keys`
  - `app config parse does not warn for all known top-level keys`
  - `app config parse logs error for wrong type on known key`
- [x] Build: `cmake --build build --target draxul draxul-tests --parallel`
- [x] Run: `./build/tests/draxul-tests` — 99 tests pass (1 pre-existing nvim-spawn failure)
- [x] Smoke: `./build/draxul --smoke-test` — passes
- [x] Format: clang-format applied to touched files.
