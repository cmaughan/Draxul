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

## Investigation Steps

- [ ] Read `libs/draxul-config/src/app_config_io.cpp` — find the TOML parse call and error handling
- [ ] Read `libs/draxul-config/include/draxul/config_document.h` — identify the error reporting path
- [ ] Identify the TOML library in use (`cmake/FetchDependencies.cmake`) and check its error type for line/column fields
- [ ] Confirm the error reaches the user via log, dialog, or toast

---

## Implementation

- [ ] In the TOML parse error handler, extract `error.line()` (or equivalent) from the parse exception
- [ ] Include the line number in the error message: `"config.toml parse error at line 42: unexpected character"`
- [ ] If WI 22 (toast notifications) is landed, surface the error as a toast; otherwise log at `ERROR` level with line number
- [ ] Add a test: write a `config.toml` with a known error on a known line; verify the error message contains the correct line number

---

## Acceptance Criteria

- [ ] Config parse errors include line number in the message
- [ ] Test verifies line number extraction
- [ ] No regression in valid config loading

---

## Notes

Small, self-contained. No interdependencies except optionally WI 22.
