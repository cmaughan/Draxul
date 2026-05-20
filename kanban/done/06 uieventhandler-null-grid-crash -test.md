# 06 uieventhandler-null-grid-crash -test

**Type:** test
**Priority:** 6
**Source:** Claude review (review-latest.claude.md)

## Problem

`UiEventHandler::process_redraw()` is called before `set_grid()` in some code paths. If the internal grid pointer is null at that point, the current code will dereference null and crash without a clean diagnostic. There is no test verifying the graceful-failure path.

After item `01 uieventhandler-raw-pointer-safety -bug` adds null assertions, this test locks in the defensive behaviour.

## Acceptance Criteria

- [x] Read `UiEventHandler` source and the fix from item 01.
- [x] Add a test that:
  - [x] Constructs `UiEventHandler` without calling `set_grid()`.
  - [x] Calls `process_redraw()` (or the equivalent entry point).
  - [x] Asserts the call produces a clean early-return or fires the expected assertion — NOT a segfault or silent memory corruption.
- [x] If the assertion fires via `DRAXUL_ASSERT`, verify it is caught under test (check the test harness's assertion override if one exists).
- [x] Run under `ctest` and `mac-asan`.

## Implementation Notes

- Use `Catch2` `REQUIRE_THROWS` or the project's own assertion-hook mechanism if assertions are not exceptions.
- Keep the test minimal — its sole purpose is to document and lock in the null guard behaviour.

## Interdependencies

- **Depends on:** `01 uieventhandler-raw-pointer-safety -bug` (needs the null guard to be in place first).

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
