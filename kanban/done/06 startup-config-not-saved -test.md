# 06 startup-config-not-saved -test

**Priority:** HIGH
**Type:** Test
**Raised by:** Gemini, GPT (validates 00-bug fix)
**Model:** claude-sonnet-4-6

---

## Problem

There is no automated test that verifies a failed `App::initialize()` does not persist config to disk. Without this test, the fix for **00-bug** can regress silently.

---

## Implementation Plan

- [x] Read existing tests under `tests/` to understand the test harness and fixture patterns.
- [x] Read `app/app.cpp` `initialize()` and `shutdown()` to identify the config-save path.
- [x] Write a test in the `draxul-tests` target (or a new file `tests/startup_config_tests.cpp`):
  - Create a temp `config.toml` with known content.
  - Trigger a deliberate init failure (e.g., inject a bad renderer or window path — look for existing seams in `AppConfig` or the init flags).
  - Assert that `config.toml` is byte-for-byte identical after the failed init.
- [x] Also test: a *successful* init followed by a config change and normal exit does persist config (positive case).
- [x] Add the new test file to `tests/CMakeLists.txt`.
- [x] Run `cmake --build build --target draxul-tests && ctest --test-dir build -R draxul-tests`.

---

## Acceptance

- Test fails before the **00-bug** fix is applied (red).
- Test passes after the fix (green).
- Positive-case test (config saved on clean exit) continues to pass.

---

## Interdependencies

- Depends on **00-bug** fix being implemented first (or developed in tandem as TDD).
- Related: **14-test** (config-partial-init-save) covers a broader scenario.
