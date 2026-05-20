# 14 config-partial-init-save -test

**Priority:** MEDIUM
**Type:** Test
**Raised by:** Gemini (broader coverage for 00-bug fix)
**Model:** claude-sonnet-4-6

---

## Problem

Beyond the single case of "failed startup must not save config" (**06-test**), there are additional scenarios around config persistence that need deterministic coverage:
- Partial init (window created, renderer fails): must not save.
- Double-shutdown (App::shutdown() called twice): must not double-write or corrupt config.
- Config mutation during a failed font-load init step: must not persist the partial change.

---

## Implementation Plan

- [x] Read `app/app.cpp` `initialize()` in detail, noting each init step and what state is set at each step.
- [x] Write `tests/config_lifecycle_tests.cpp`:
  - **Partial-init test**: Use a test hook or config flag to cause failure at each of several init stages (window OK, renderer FAIL; window OK, renderer OK, font FAIL). Assert config unchanged after each failure.
  - **Double-shutdown test**: Call `shutdown()` twice on a properly initialized App (use a headless/stub configuration if available). Assert config is written exactly once, file is not corrupted.
  - **Config mutation + failure test**: Modify an AppConfig value, then trigger an init failure before the config is normally persisted. Assert the disk file still has the old value.
- [x] Mock or stub the renderer/font initialisation as needed to inject deliberate failures. Look for existing test seams before adding new ones.
- [x] Add to `tests/CMakeLists.txt`.
- [x] Run ctest.

---

## Acceptance

- All three scenario categories pass.
- Tests are deterministic (no timing dependencies, no real GPU required if possible).

---

## Interdependencies

- Depends on **00-bug** fix being in place (otherwise many of these tests will trivially fail due to the underlying bug).
- Companion to **06-test** (startup-config-not-saved).
