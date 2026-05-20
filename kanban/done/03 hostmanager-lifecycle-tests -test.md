# 03 hostmanager-lifecycle-tests -test

**Priority:** HIGH
**Type:** Test (behavioral coverage for high-risk orchestrator)
**Raised by:** GPT (strongly), Claude (agreed)
**Model:** claude-sonnet-4-6

---

## Problem

`tests/host_manager_tests.cpp` only verifies shell-kind selection. The `HostManager` class owns the most important lifecycle policy in the application — host creation, split semantics, close/focus reassignment, pane hit-testing, viewport recomputation, and the 3D-host capability attach path — but none of those state transitions are covered. This is a severe mismatch between risk and coverage, especially as multi-pane and multi-agent additions land.

---

## Implementation Plan

- [x] Read `app/host_manager.h` and `app/host_manager.cpp` in full. Map out all state transitions.
- [x] Read `tests/host_manager_tests.cpp` to understand existing coverage baseline.
- [x] Read `tests/fakes/` to understand what fake hosts/renderers/windows already exist for injection.
- [x] Write tests covering:
  - [x] **Host creation failure rollback** — if `IHost::initialize()` returns false, the manager does not leave a partially-initialized host in any slot.
  - [x] **Split semantics** — splitting a pane produces two active panes; both get correct viewport fractions; both receive `set_viewport()` calls.
  - [x] **Close semantics** — closing a pane removes it from the active set; focus is reassigned to an adjacent pane; no dangling host references remain.
  - [x] **Focus reassignment** — after close, `host()` returns the newly-focused pane.
  - [x] **Hit-testing** — given a pixel coordinate, `host_at()` (or equivalent) returns the correct pane in a 2-pane layout.
  - [x] **3D-host attach path** — a fake `I3DHost` receives `attach_3d_renderer()` during `HostManager::initialize()` and not before.
  - [x] **Viewport recomputation on resize** — when the window resizes, all panes receive updated `set_viewport()` calls proportional to their split fractions.
- [x] Consider using a subagent to write the test file once the interfaces are understood, since it is large and mechanical.
- [x] Build and run: `cmake --build build --target draxul-tests && ctest`.

---

## Acceptance

- The new tests cover all state transitions listed above.
- All tests pass on a clean build.
- No changes to production code required (tests should be design-compatible with current API).

---

## Interdependencies

- `00-bug` (multi-pane timing) — the timing fix is easier to validate after these lifecycle tests exist.
- `04-test` (app smoke test) — can be developed in parallel; smoke test is higher-level.
- `16-refactor` (icebox: HostManager dynamic_cast removal) — these tests provide the safety net needed before that refactor.

---

*claude-sonnet-4-6*
