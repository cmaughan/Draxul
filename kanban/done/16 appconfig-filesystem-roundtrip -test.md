# 16 AppConfig Filesystem Round-Trip Test

## Why This Exists

`AppConfig::load()` and `AppConfig::save()` read and write the user's `config.toml`. The existing parse tests cover the in-memory parsing logic but do not exercise the filesystem path. If `load()` silently swallows a missing file, or `save()` truncates on write error, or the path calculation is wrong, the config is silently lost.

**Source:** `app/app_config.cpp`, `app/app_config.h`.
**Raised by:** Claude, GPT (both list this as a top missing test).

## Goal

Add tests that exercise `AppConfig` through actual filesystem I/O using a temp directory:

1. `save()` to a temp path, then `load()` from the same path — verify all fields round-trip correctly.
2. `load()` from a nonexistent path — verify it returns a default config (or fails gracefully with a logged error).
3. `save()` to a read-only directory — verify it logs an error and does not crash.
4. `load()` from a file with a partial/corrupt line — verify the parser does not crash and returns sane defaults for the bad fields.

## Implementation Plan

- [x] Read `app/app_config.cpp` and `app/app_config.h` to understand the load/save API and the fields.
- [x] Check how the config path is resolved — determine if there is a way to override the path for testing (constructor parameter, static path function, etc.). Add a path-override if needed.
- [x] Write tests using `std::filesystem::temp_directory_path()` / `std::tmpnam` for a safe temp file:
  - `appconfig_save_load_roundtrip`
  - `appconfig_load_missing_file_returns_defaults`
  - `appconfig_load_corrupt_line_does_not_crash`
- [x] Add tests to `draxul-tests` (new file `tests/app_config_fs_tests.cpp` or extend existing).
- [x] Run `ctest --test-dir build`.

## Notes

If `AppConfig` does not currently support a path override for testing, add a minimal constructor
or factory parameter rather than changing the default path resolution. Keep the change narrow.

Implemented by adding `load_from_path()` / `save_to_path()` plus filesystem-backed tests in
`tests/app_config_tests.cpp`. The write-failure case uses a non-directory parent path to exercise
the same warning path cross-platform without relying on platform-specific read-only semantics.

## Sub-Agent Split

Single agent. May need to add a small testability seam to `AppConfig` if the path is currently hardcoded.
