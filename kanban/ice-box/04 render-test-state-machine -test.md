# Test: Unit Tests for run_render_test() State Machine

**Type:** test
**Priority:** 4
**Source:** Claude, GPT, Gemini (all three flagged this as a major testing gap)

## Problem

`App::run_render_test()` in `app/app.cpp` controls the render-test capture workflow. It uses 7+ uncoordinated `std::optional<>` flags to track state:

- `diagnostics_enabled`, `ready_since`, `quiet_since`, `capture_requested`, and others

There are no unit tests for these state transitions. A missed transition silently causes a CI timeout with no diagnostic. The only way bugs surface is in a full CI render-test run.

**Note:** This test work item should be done in the same agent pass as `13 render-test-state-machine -refactor`. The refactor makes the code testable; the tests validate the refactor.

## Investigation steps

- [ ] Read `app/app.cpp` and identify all `std::optional<>` flags used by `run_render_test()`.
- [ ] Map out the expected state transitions (startup → waiting for quiet → capture → compare → exit).
- [ ] Check how the function interacts with the renderer (captures a frame), the clock (tracks quiet time), and the file system (writes/compares BMP).
- [ ] Check whether any test seams exist (fake clock, fake renderer) or need to be added.

## Test design

The tests should be in `tests/render_test_state_machine_tests.cpp` or appended to an existing app-layer test file.

Write tests for:
- [ ] **Happy path**: startup → frames rendered → quiet period elapses → capture triggered → compare succeeds → result returned.
- [ ] **Timeout**: quiet period never elapses (always dirty) → timeout fires → failure result.
- [ ] **Capture fails**: renderer returns an error → failure result with clear message.
- [ ] **Compare fails**: captured BMP differs from reference → failure result with diff path.
- [ ] **Diagnostics disabled path**: verify diagnostics are disabled on entry and re-enabled on exit.
- [ ] **Multiple frames before quiet**: ensure "quiet" timer resets on each non-quiet frame.

Fake/stub needs:
- [ ] A fake frame clock that advances by a controlled delta.
- [ ] A fake renderer that can be told to return a specific capture result.
- [ ] A fake file system or temp directory for BMP comparison.

## Acceptance criteria

- [ ] All state-transition scenarios above are covered.
- [ ] Tests run under `ctest` as part of the `draxul-tests` target.
- [ ] No test depends on real timing (wall-clock sleep); all use injected/fake clocks.

## Interdependencies

- **`13 render-test-state-machine -refactor`**: the refactor (replacing optionals with a state enum) is a prerequisite — do both in the same agent pass.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
