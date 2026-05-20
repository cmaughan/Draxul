# WI 54 — grid-handle-null-init-test

**Type**: test  
**Priority**: 6 (regression guard for WI 48)  
**Source**: review-consensus.md §T3 [P]  
**Produced by**: claude-sonnet-4-6

---

## Problem / Goal

After WI 48 (vk-null-grid-handle-dereference) is fixed, automated tests must confirm that both `GridHostBase::initialize()` and `CommandPaletteHost::dispatch_action()` handle a null handle gracefully rather than crashing.

---

## Pre-condition

**WI 48 must be merged before writing these tests.**

---

## Tasks

- [x] Read `libs/draxul-host/src/grid_host_base.cpp` and the `GridHostBase::initialize()` method — understand what a null-handle failure result looks like after the WI 48 fix.
- [x] Read `app/command_palette_host.cpp` and the `dispatch_action()` method — understand the null-handle early-return path.
- [x] Read `tests/support/fake_renderer.h` — determine whether the fake renderer's `create_grid_handle()` can be configured to return `nullptr`.
- [x] Added a `fail_create_grid_handle` flag to both `FakeTermRenderer` and `FakeGridPipelineRenderer` so existing tests are unaffected; flipping the flag makes `create_grid_handle()` return `nullptr` and bumps `create_grid_handle_calls` so tests can assert it was reached.
- [x] Write test: `GridHostBase::initialize()` with a null-returning renderer returns false and does not invoke the subclass `initialize_host()` hook.
- [x] Write test: `CommandPaletteHost::dispatch_action("toggle")` with a null-returning renderer logs an error, returns true (action handled), and leaves the palette inactive without crashing.
- [x] Build and run: `cmake --build build --target draxul-tests && ctest --test-dir build -R draxul-tests`

---

## Acceptance Criteria

- [x] At least 2 new test cases. (3 added in `tests/grid_host_null_handle_tests.cpp`.)
- [x] Both tests pass.
- [x] No existing test regressions.

---

## Interdependencies

- **Requires WI 48** merged first.
- Modifying `FakeRenderer` to support null-handle mode should be backward-compatible; all existing tests using `FakeRenderer` must still pass.

---

## Notes for Agent

- Prefer extending the existing fake rather than creating a separate renderer double, to keep the test infrastructure lean.
- Do not write tests that require a real GPU or Vulkan driver; use the fake renderer throughout.
