---
# WI 79 — Config Parse Errors Should Include Line Numbers

**Type:** feature
**Priority:** medium (user-facing error quality)
**Raised by:** [C] Claude
**Created:** 2026-04-03
**Model:** claude-sonnet-4-6

---

## Problem

When `config.toml` has a syntax error (e.g. missing quote, bad value), the current error message omits the line number. TOML parse errors from the underlying library include position information, but it is not surfaced to the user. This makes diagnosing config errors unnecessarily difficult.

---

## Completion Evidence

The file-load parse path already preserves toml++ source positions by streaming `toml::parse_error` in `libs/draxul-config/include/draxul/toml_support.h`; `AppConfig::load_from_path()` logs that string from `libs/draxul-config/src/app_config_io.cpp`.

Added regression coverage in `tests/corrupt_config_recovery_tests.cpp`:

- `corrupt config: parse failure warning includes source line number`
- Writes malformed TOML with the syntax error on line 4
- Verifies the captured warning includes `Failed to parse config` and `line 4`

Verified with:

```powershell
cmake --build build --config Debug --target draxul-tests -- /m:4
.\build\tests\Debug\draxul-tests.exe "corrupt config: parse failure warning includes source line number"
```

---

## Investigation Steps

- [x] Read `libs/draxul-config/src/app_config_io.cpp` — find the TOML parse call and error handling
- [x] Read `libs/draxul-config/include/draxul/config_document.h` — identify the error reporting path
- [x] Identify the TOML library in use (`cmake/FetchDependencies.cmake`) and check its error type for line/column fields
- [x] Confirm the error reaches the user via log, dialog, or toast

---

## Implementation

- [x] Preserve toml++ source-position information from `toml::parse_error` in the parse error string
- [x] Include the line number in the logged error message, e.g. `line 4`
- [x] Keep the existing config-load WARN path; no toast/error-level escalation was added for this card
- [x] Add a test: write a `config.toml` with a known error on a known line; verify the error message contains the correct line number

---

## Acceptance Criteria

- [x] Config parse errors include line number in the message
- [x] Test verifies line number extraction
- [x] No regression in valid config loading

---

## Notes

Small, self-contained. No interdependencies except optionally WI 22.
