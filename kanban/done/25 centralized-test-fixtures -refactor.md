# WI 25 — Centralised test fixtures (FakeWindow, FakeRenderer, FakeHost)

**Type:** refactor  
**Source:** review-latest.claude.md  
**Consensus:** review-consensus.md Phase 6

---

## Goal

Create a shared test-support library containing canonical `FakeWindow`, `FakeRenderer`, `FakeHost`, `FakeGridHandle`, and `FakeClock` implementations so that new tests (WI 14-23 and beyond) do not each reinvent their own mocks.

---

## Problem

Claude's review noted that test fixtures are scattered — each test file reinvents mocks for `IWindow`, `IGridRenderer`, `IHost`, etc. Consequences:
- Adding a new interface method requires updating every independent mock.
- Inconsistent mock behaviour makes test failures ambiguous.
- New-host work (MegaCity, ChromeHost variants) requires understanding many mock patterns before writing a single test line.

`replay_fixture.h` is already a good example of a shared fixture; this extends the pattern to the renderer/window/host layer.

---

## Implementation Plan

- [x] Audit existing test files for mock implementations of `IWindow`, `IGridRenderer`, `IHost`, `IGridHandle`, and any time/clock abstractions.
- [x] Extract the canonical (most complete) version of each mock into a new file in `tests/support/` (which already contains `replay_fixture.h`).
  - [x] `tests/support/fake_window.h` (already existed; no further changes needed).
  - [x] `tests/support/fake_renderer.h` (already existed; now includes the split-out `fake_grid_handle.h`).
  - [x] `tests/support/fake_host.h` — new. Canonical `FakeHost` built from `SmokeTestHost`/`StubHost`/`DispatchTrackingHost` with behaviour flags (`fail_initialize`, `is_nvim`, `request_frame_on_pump`, `dispatch_action_result`) and call-count/argument-capture members for every IHost method.
  - [x] `tests/support/fake_grid_handle.h` — new. `FakeGridHandle` extracted from `fake_renderer.h`; records batches, overlays, cursor, viewport, default bg, scroll offset.
  - [x] `tests/support/fake_clock.h` — new. `FakeClock` extracted from `toast_host_tests.cpp`; exposes `advance()`, `now()`, `set_now()`, and `source()` returning a `std::function<time_point()>` usable as any `TimeSource`.
- [x] Each fake exposes:
  - [x] Constructor / member toggles for behaviour injection (e.g. `FakeGridPipelineRenderer::fail_create_grid_handle` returning null; `FakeHost::fail_initialize`; `FakeHost::is_nvim`).
  - [x] Call-count / argument-capture members for assertion.
- [x] Update existing tests to use the shared fakes rather than their local copies (do this incrementally — at minimum the tests being added by WI 14-23 should use the shared fakes from day one).
  - [x] `tests/scrollback_overflow_tests.cpp` — uses shared `FakeWindow` (was `SbFakeWindow`).
  - [x] `tests/shell_host_crash_tests.cpp` — uses shared `FakeWindow` (was `ShCrashFakeWindow`).
  - [x] `tests/terminal_vt_psreadline_tests.cpp` — uses shared `FakeWindow` (was `PsFakeWindow`).
  - [x] `tests/toast_host_tests.cpp` — uses shared `FakeClock` (was local `struct FakeClock`).
  - [x] `tests/input_dispatcher_routing_tests.cpp` — uses shared `FakeHost` via `using StubHost = tests::FakeHost` (was local `StubHost`).
  - [x] `tests/app_dispatch_tests.cpp` — `DispatchTrackingHost` now subclasses shared `FakeHost` (removes ~100 lines of boilerplate).
  - [x] `tests/app_smoke_tests.cpp` — `SmokeTestHost`, `FailingInitHost`, `InitFrameOnlyHost` now built on shared `FakeHost`.
  - [x] `tests/host_manager_tests.cpp` — `LifetimeTestHost` is now a `FakeHost` alias; `GuardedGridHost` replaced by `FakeGridHost`. `on_shutdown_callback` + `fire_callback_on_shutdown` added to shared `FakeHost` for post-destruction shutdown tracking.
  - [x] `tests/grid_host_null_handle_tests.cpp` — `StubGridHost` replaced by shared `FakeGridHost` (`tests/support/fake_grid_host.h`).
- [x] Wire the new support files into `CMakeLists.txt` as part of the `draxul-tests` target.
  - `tests/support/` is already on the include path and `*_tests.cpp` is globbed with `CONFIGURE_DEPENDS`; no CMake changes were needed.

---

## Notes for the agent

- This is primarily a reorganisation task; no new test logic is being introduced.
- The test harness changes here will immediately pay dividends for WI 14, 16, 18, 19, 20, 22, and 23.
- Consider doing this as a prerequisite or early parallel task before the Wave 5 tests.

---

## Interdependencies

- Enables WI 14, 16, 17, 18, 19, 20, 22, 23 — they all need these fixtures.
- WI 22 (inputdispatcher-null-deps) needs `FakeGridHandle` with a null-return mode.
- WI 19 (toasthost-init-failure) needs `FakeGridHandle` that injects null.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
