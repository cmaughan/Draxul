---
# WI 69 — Concurrent Host Shutdown and Event Dispatch Tests

**Type:** test  
**Priority:** medium (rare but high-severity dangling pointer risk)  
**Raised by:** [C] Claude, [P] GPT  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

There is an untested race window: a host process can exit between `pump()` and `close_dead_panes()` in the same frame. The `InputDispatcher` holds a pointer to the active host. If the pane is destroyed before `InputDispatcher::set_host(nullptr)` is called, the next input event dispatches to a dangling pointer.

A related scenario from [P]: `DiagnosticsPanelHost` visibility is toggled during a render test mid-frame. The `last_render_time()` counter must advance correctly and the `EnablingDiagnostics → SettlingForCapture` phase transition must fire only after the panel has actually rendered.

---

## Investigation Steps

- [ ] Read `app/app.cpp` around `pump_once()` / `close_dead_panes()` call order
- [ ] Read `app/input_dispatcher.cpp` to understand when `set_host(nullptr)` is called
- [ ] Verify `close_dead_panes()` calls `set_host(nullptr)` before destroying the pane
- [ ] Read `app/app.cpp` `RenderTestPhase` state machine for the diagnostics-during-render-test scenario

---

## Test Cases

### Scenario A — Host exits between pump and close
- [ ] Create a fake host that reports `is_dead()` after the first `pump()` call
- [ ] Drive one frame through the app loop
- [ ] Verify `InputDispatcher::set_host(nullptr)` is called before the host is destroyed
- [ ] Verify no subsequent input event reaches the dead host

### Scenario B — DiagnosticsPanelHost during render test
- [ ] Set up a fake renderer and app loop in render-test mode
- [ ] Toggle diagnostics panel visibility mid-run
- [ ] Verify `last_render_time()` advances on each frame
- [ ] Verify phase transition from `EnablingDiagnostics` to `SettlingForCapture` occurs only after panel renders

---

## Implementation

- [ ] Create `tests/host_shutdown_concurrent_test.cpp`
- [ ] Use fake host, fake renderer, and a controlled event loop tick

---

## Acceptance Criteria

- [ ] Scenario A: no access to destroyed host in any code path
- [ ] Scenario B: phase transition timing is deterministic with a fake clock

---

## Notes

A subagent is appropriate here if the render-test state machine tests (WI 04) are being worked concurrently — the `RenderTestPhase` tests in WI 04 cover the state machine itself; this item covers the host lifecycle interaction with it.
