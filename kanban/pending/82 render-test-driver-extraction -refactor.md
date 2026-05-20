---
# WI 82 — Extract Render-Test Orchestration Out of `App` into `draxul-render-test`

**Type:** refactor
**Priority:** medium (cleans up `App`, isolates test-only code in a test-only library)
**Raised by:** [P] human
**Created:** 2026-04-07
**Model:** claude-opus-4-6

---

## Problem

Render-test orchestration is split awkwardly between `app/` and `libs/draxul-render-test/`:

- `libs/draxul-render-test/` already owns the "boring" pieces: scenario TOML loading, image export, finalize/bless reporting (`render_test.{h,cpp}`), and the NanoVG demo content host (`nanovg_demo_host.{h,cpp}`).
- `app/app.cpp:795` still owns `App::run_render_test()` — a ~180-line state machine (`RenderTestPhase`: WaitingForContent → SettlingContent → EnablingDiagnostics → SettlingForCapture → Capturing) plus its timeout/error formatter and the `RenderTestPhase` / `RenderTestContext` types declared in an anonymous namespace at the top of `app.cpp`.
- `App::run_screenshot()` at `app/app.cpp:762` is a smaller cousin (~30 lines) with the same pump-and-capture pattern.
- `AppOptions` (`libs/draxul-config/include/draxul/app_options.h:32-40`) carries three render-test-specific fields (`show_diagnostics_in_render_test`, `show_render_test_window`, `render_target_pixel_width/height`) that leak test concerns into the production options struct. Note: `render_target_pixel_*` is shared with `--screenshot`, so it is not purely render-test-only.

The result: `App` is cluttered with test-driver state, the state machine has no unit tests, and adding a new phase or condition means editing `app.cpp` rather than the test library that owns the rest of the render-test surface.

---

## Why "make it a host like NanoVGDemoHost" does not fit

The user's first instinct was to model it as a host inside `draxul-render-test`, parallel to `NanoVGDemoHost`. That analogy breaks down:

- `NanoVGDemoHost` is a **content** host — it draws into a viewport.
- The render-test orchestrator is a **driver** — it draws nothing, and instead pokes:
  - `active_host_manager().host()` for `runtime_state()`
  - `diagnostics_host_` to enable the panel and wait for it to render
  - `chrome_host_` for the tab-bar height when recomputing viewports
  - `renderer_.capture()` for `request_frame_capture` / `take_captured_frame`
  - `pump_once()`, `request_frame()`, `running_`, `saw_frame_`, `frame_requested_` on `App`

If forced into `IHost`, almost every method (`draw`, `set_viewport`, `default_background`, `pump`, `next_deadline`) would be a no-op, and the driver would still need backdoor access to the subsystems above. It would also confuse `host_manager_` focus, viewport recomputation, and lifecycle.

The cleaner shape is a free-standing class behind a small env interface.

---

## Investigation Steps

- [ ] Read `app/app.cpp:30-90` (RenderTestPhase / RenderTestContext / phase-name helper) and `app.cpp:795-975` (`run_render_test`).
- [ ] Read `app/app.cpp:762-793` (`run_screenshot`) — decide whether to bring it into scope.
- [ ] Read `libs/draxul-render-test/include/draxul/render_test.h` and `src/render_test.cpp` to confirm the existing seam.
- [ ] Check `app/main.cpp:84-130, 292-396` to see how render-test flags flow from CLI → `AppOptions` → `App::run_render_test()` → finalize/export.
- [ ] Confirm there are no existing unit tests for the render-test state machine (`tests/` does not appear to cover it).

---

## Proposed Design

Add a `RenderTestDriver` to `libs/draxul-render-test/`:

```cpp
// libs/draxul-render-test/include/draxul/render_test_driver.h
namespace draxul {

struct RenderTestDriverEnv {
    // App implements these inline as lambdas capturing `this`.
    std::function<bool(std::chrono::steady_clock::time_point)> pump_once;
    std::function<void()> request_frame;
    std::function<bool()> is_running;
    std::function<bool()> saw_frame;
    std::function<bool()> frame_requested;
    std::function<std::optional<HostRuntimeState>()> active_host_state;
    std::function<void()> enable_diagnostics_panel;       // no-op when not wanted
    std::function<std::optional<std::chrono::steady_clock::time_point>()>
        diagnostics_panel_render_time;
    std::function<ICaptureRenderer*()> capture;
};

struct RenderTestDriverOptions {
    std::chrono::milliseconds timeout;
    std::chrono::milliseconds settle;
    bool want_diagnostics;
};

struct RenderTestDriverResult {
    std::optional<CapturedFrame> frame;
    std::string error;       // empty on success
};

RenderTestDriverResult run_render_test_driver(
    RenderTestDriverEnv& env, RenderTestDriverOptions opts);

} // namespace draxul
```

The state machine, the phase enum, the phase-name helper, and the timeout-error formatter all move into `render_test_driver.cpp`.

`App::run_render_test()` collapses to ~30 lines: build the env (lambdas capturing `this`), call `run_render_test_driver`, copy `result.error` into `last_render_test_error_`, return `result.frame`.

---

## Implementation Steps

- [ ] Add `libs/draxul-render-test/include/draxul/render_test_driver.h` and `src/render_test_driver.cpp`. Wire into `libs/draxul-render-test/CMakeLists.txt`.
- [ ] Move `RenderTestPhase`, `RenderTestContext`, `render_test_phase_name`, and the body of `App::run_render_test` (the state machine + timeout-error formatter) into `render_test_driver.cpp`.
- [ ] Replace `App::run_render_test()` with a thin shim that constructs `RenderTestDriverEnv`, calls `run_render_test_driver`, and stores the error string.
- [ ] Confirm `app/main.cpp` still compiles unchanged — the render-test CLI flow only depends on `App::run_render_test()`'s public signature, which is preserved.
- [ ] Add unit tests in `tests/render_test_driver_tests.cpp` covering at least:
  - Happy path with `want_diagnostics = false` — content_ready → settle → capture.
  - Happy path with `want_diagnostics = true` — content → settle → enable diagnostics → wait for panel render → capture.
  - Timeout when content never becomes ready (verifies error string contents).
  - Timeout when capture is requested but `take_captured_frame` never returns.
  Use a fake `RenderTestDriverEnv` whose closures advance scripted state per `pump_once` call.
- [ ] Run `cmake --build build --target draxul draxul-tests` and `ctest --test-dir build -R draxul-tests`.
- [ ] Run `py do.py smoke` to confirm the app still launches.

---

## Optional Follow-Ups (defer unless trivially in scope)

- [ ] Extract a similar `run_screenshot_capture(env, delay)` helper alongside the driver — `App::run_screenshot()` shares the pump-and-capture pattern. ~30 lines, dedupes timeout handling.
- [ ] Move `show_diagnostics_in_render_test` and `show_render_test_window` out of `AppOptions` into a `RenderTestRuntimeOptions` struct passed through `main.cpp`. Skip `render_target_pixel_*` since it is shared with `--screenshot`.

---

## Acceptance Criteria

- [ ] `app/app.cpp` no longer contains `RenderTestPhase`, `RenderTestContext`, `render_test_phase_name`, or the render-test state-machine body.
- [ ] `App::run_render_test()` is a thin shim (~30 lines) that delegates to `run_render_test_driver`.
- [ ] `last_render_test_error()` continues to return the same error strings (verified by an existing failing scenario or a unit test).
- [ ] New unit tests cover the four cases above and pass under the `mac-asan` preset.
- [ ] All existing render-test scenarios (`py do.py blessall` outputs match) pass unchanged.
- [ ] `py do.py smoke` passes.

---

## Notes

- The driver does not become a host — that was considered and rejected (see "Why not a host" above).
- Keep CLI parsing in `app/main.cpp`. It is small and the natural place for option-flag → driver wiring.
- `nanovg_demo_host.{h,cpp}` stays where it is; it is content, already in `draxul-render-test`, and unrelated to this move.
- The state machine currently has zero unit-test coverage. Adding fakes-based tests is a significant side benefit of this extraction and should not be deferred to a follow-up.
