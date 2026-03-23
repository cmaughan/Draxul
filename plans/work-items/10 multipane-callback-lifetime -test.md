# 10 multipane-callback-lifetime -test

**Priority:** HIGH
**Type:** Test (use-after-free safety for HostCallbacks during multi-pane teardown)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`App::make_host_callbacks()` creates `HostCallbacks` lambdas that capture `App* this`. If a host fires a callback during `shutdown()` after `App` is partially torn down, the lambda accesses freed memory. The `running_` flag is the only guard and it is not safe for multi-pane scenarios where one host shuts down while another fires. There is no test exercising this lifetime window.

---

## Code Locations

- `app/app.cpp` — `make_host_callbacks()` and the `running_` guard
- `libs/draxul-host/include/draxul/host.h:66-73` — `HostCallbacks` struct with 5 `std::function` slots
- `app/host_manager.cpp` — `HostManager` lifecycle
- `tests/app_smoke_tests.cpp` — existing app-level test patterns

---

## Implementation Plan

- [ ] Read `app.cpp` `make_host_callbacks()` and `HostManager::create_host_for_leaf()` to understand the full callback wiring.
- [ ] Read `tests/app_smoke_tests.cpp` to understand how the app is constructed with fake hosts in tests.
- [ ] Design a two-pane test scenario using the DI seams (`host_factory`):
  - Use a `FakeHost` that records whether its callbacks were fired after shutdown.
  - Create a two-pane split (`HostManager::split()`).
  - Shut down pane 1 (call `HostManager::remove_leaf()` or equivalent + host destructor).
  - Fire a callback from pane 2.
  - Assert pane 1's callbacks are not invoked after teardown.
- [ ] Also test: after the `App` enters shutdown (`running_ = false`), a callback fired from any host should be a no-op, not a crash.
- [ ] Use ASan build (`cmake --preset mac-asan`) to catch the use-after-free if it exists before the fix.
- [ ] Build and run tests.
- [ ] Run `clang-format`.

---

## Acceptance Criteria

- Test demonstrates the (potential) lifetime issue and passes after the fix in `15 hostcallbacks-virtual-interface -refactor`.
- If the current `running_` guard is already sufficient, the test proves it by passing without the refactor.
- No regression in existing app smoke tests.

---

## Interdependencies

- **`15 hostcallbacks-virtual-interface -refactor`** — the refactor is the full fix; this test is the motivating evidence.
- Can be written independently of the refactor; run under ASan to detect issues.

---

*claude-sonnet-4-6*
