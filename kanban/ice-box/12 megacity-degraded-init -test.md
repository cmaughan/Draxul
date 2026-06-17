# Test: MegaCityHost Graceful Degraded Initialisation

**Type:** test
**Priority:** 12
**Source:** Gemini review

## Problem

`MegaCityHost` has no tests for failure and degraded-mode scenarios:

1. **Tree-sitter source unavailable**: if the configured scan root cannot be read or never yields a completed snapshot, the host should initialise in a degraded mode (city visible but no symbol data), not crash or hang.
2. **Sign text service unavailable**: if the text service for 3D sign labels fails to initialise, the host should fall back gracefully.
3. **Unchanged snapshot**: processing the same snapshot twice should not trigger an unnecessary city rebuild (performance invariant).

Without these tests, any regression in error handling silently crashes or hangs the app on startup when the MegaCity source path is inaccessible.

**Note:** Do alongside `09 codebasescanner-lifecycle -test` where scanner fixture setup can be shared.

## Investigation steps

- [ ] Read `libs/draxul-megacity/src/megacity_host.cpp` — find the `initialize()` method and all error paths.
- [ ] Find where the Tree-sitter semantic source is configured and how errors are handled.
- [ ] Find where the sign text service is initialised.
- [ ] Check whether `MegaCityHost` has existing tests in `tests/`.

## Test design

Add to `tests/megacity_host_tests.cpp` (create if needed). Use a fake/stub renderer.

### Source unavailable

- [ ] Pass a source root in a non-existent or unreadable directory.
- [ ] Call `MegaCityHost::initialize()`.
- [ ] Assert: `initialize()` returns a degraded-OK result (or sets a degraded flag), not an unrecoverable error.
- [ ] Assert: subsequent `pump()` calls do not crash.
- [ ] Assert: a `WARN` or `ERROR` is logged.

### Sign text service unavailable

- [ ] Inject a null or failing text-service stub.
- [ ] Call `initialize()` and `pump()` through a few frames.
- [ ] Assert: no crash, no hang, sign rendering is silently skipped.

### Unchanged snapshot no-rebuild

- [ ] Trigger a reconcile with snapshot A.
- [ ] Trigger reconcile with the same snapshot A again.
- [ ] Assert: the city rebuild/layout step is not triggered a second time (check a rebuild counter or mock).

## Acceptance criteria

- [ ] All three scenarios pass under ASan.
- [ ] Tests are part of `draxul-tests` and do not require a real renderer or display.

## Interdependencies

- **`09 codebasescanner-lifecycle -test`**: share scanner fixture setup where practical.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
