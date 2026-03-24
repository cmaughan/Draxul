# 12 corrupt-config-recovery -test

**Type:** test
**Priority:** 12
**Source:** Gemini review (review-latest.gemini.md)

## Problem

There is no test that verifies Draxul starts with sensible defaults when `config.toml` is missing, unreadable, or contains garbage data. Config parsing errors may silently produce zero/default values, or may crash, depending on the TOML parser's error handling and how `AppConfig` handles parse failures.

## Acceptance Criteria

- [x] Read the config loading path in `libs/draxul-app-support/` (likely `app_config.cpp` / `config_loader.cpp`).
- [x] Add tests for:
  - [x] Missing `config.toml` — app should initialise with defaults, no crash.
  - [x] Empty `config.toml` — should produce defaults.
  - [x] Garbage/binary content in `config.toml` — should produce a WARN log + defaults, no crash.
  - [x] `config.toml` with valid TOML but unknown keys — should log WARN for unknown keys and use defaults for missing required keys.
  - [x] `config.toml` with out-of-range values (e.g. `scroll_speed = 999.0`) — should clamp and WARN.
- [x] Verify via `ScopedLogCapture` that warnings are emitted where expected.
- [x] Run under `ctest`.

## Implementation Notes

- Use `std::filesystem::temp_directory_path()` to create throwaway config files in tests.
- Do NOT test by mutating the real user config.
- If the config loader does not currently handle these cases gracefully, add the guards as part of this item.

## Interdependencies

- No blockers. Independent test item.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
