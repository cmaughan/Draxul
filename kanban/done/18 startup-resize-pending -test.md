# 18 Startup Deferred Resize Test

## Why This Exists

`App` tracks `startup_resize_pending_`: when a `WindowResizeEvent` arrives before the first `flush` event from Neovim, the resize is deferred. When the first `flush` eventually arrives, the deferred resize is dispatched. If this path is broken, resizing the window before the first paint is displayed will silently use the wrong grid dimensions.

This path has no test coverage.

**Source:** `app/app.cpp` — `on_resize`, `startup_resize_pending_`, first-flush dispatch.
**Raised by:** Claude.

## Goal

Add a test that simulates the deferred-startup-resize sequence:
1. Simulate a `WindowResizeEvent` arriving before any `flush`.
2. Verify that no resize RPC call is made yet.
3. Simulate the first `flush` event.
4. Verify that `queue_resize_request` is called with the post-flush dimensions.

## Implementation Plan

- [x] Read `app/app.cpp` and `app/app.h` to understand `startup_resize_pending_` and the deferred flush path.
- [x] Determine whether this is testable at the `App` level or whether it requires extracting the logic into a smaller testable object (see work item 21 — `draxul-app-support` library extraction).
  - If `App` is too big to unit test directly, consider testing the deferred logic via replay fixture + a fake `IRpcChannel`.
- [x] If App decomposition is needed first, note this as a dependency and defer to after item 21.
- [x] Write test:
  - `deferred_resize_dispatches_on_first_flush`
  - `no_resize_before_first_flush`
- [x] Add tests to `draxul-tests`.
- [x] Run `ctest --test-dir build`.

## Notes

This test may be easier after work item 21 (extract app-support library) makes `App` more decomposed.
If `App` can be cleanly tested via replay fixture at the integration level, that's acceptable.

Implemented by extracting the deferred-resize bookkeeping into `StartupResizeState` and covering
the dispatch/no-op cases directly in `tests/startup_resize_state_tests.cpp`.

## Sub-Agent Split

Single agent. May need to coordinate with work item 21 if `App` is not testable without decomposition.
