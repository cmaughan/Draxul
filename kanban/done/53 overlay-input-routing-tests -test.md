# WI 53 — overlay-input-routing-tests

**Type**: test  
**Priority**: 5 (regression guard for WI 50)  
**Source**: review-consensus.md §T2 [P][G]  
**Produced by**: claude-sonnet-4-6

---

## Problem / Goal

After WI 50 (overlay-input-incomplete-interception) is fixed, automated tests must prevent the leaky event routing from regressing. This item adds focused `InputDispatcher` tests for all four leaked paths.

---

## Pre-condition

**WI 50 must be merged before writing these tests.**

---

## Tasks

- [x] Read `app/input_dispatcher.cpp` and `app/input_dispatcher.h` to understand how `overlay_host_` is set, cleared, and checked in each event path after the WI 50 fix.
- [x] Read existing `tests/` for `InputDispatcher` tests (search for `input_dispatcher`) to find the correct test file to extend.
- [x] Write test: When an overlay host is active, a `MouseButtonEvent` is **not** forwarded to the underlying pane host.
- [x] Write test: When an overlay host is active, a `MouseWheelEvent` is **not** forwarded to the underlying pane host.
- [x] Write test: When an overlay host is active, a `TextEditingEvent` is **not** forwarded to the underlying host. (Note: actual code drops the event entirely rather than forwarding to overlay; the regression-guard is the underlying-host-not-touched assertion.)
- [x] Write test: When the overlay is cleared (`overlay_host_ = nullptr`), all event types reach the pane host again.
- [x] Write test: When an overlay is active, key + text-input events are forwarded to the **overlay** host itself.
- [x] Build and run: `cmake --build build --target draxul-tests && ctest --test-dir build -R draxul-tests`

7 new test cases added in `tests/input_dispatcher_routing_tests.cpp` under the `[overlay]` tag, exercising mouse button/wheel/move + text editing + key + text input + the cleared-overlay restoration path.

---

## Acceptance Criteria

- [x] At least 5 new test cases covering the above scenarios. (7 added.)
- [x] All tests pass.
- [x] No existing test regressions.

---

## Interdependencies

- **Requires WI 50** merged first.
- No other dependencies.

---

## Notes for Agent

- Use the existing fake host / fake renderer infrastructure from `tests/support/`.
- Tests should exercise `InputDispatcher` directly; no need to spin up a full App.
- The overlay host in tests can be a minimal `IHost` stub that records received events.
