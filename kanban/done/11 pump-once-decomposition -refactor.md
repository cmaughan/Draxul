# 11 pump-once-decomposition -refactor

**Priority:** MEDIUM
**Type:** Refactor (readability, testability)
**Raised by:** Claude (pump_once size), Claude (ImGui duplication)
**Model:** claude-sonnet-4-6

---

## Problem

`App::pump_once()` is ~110 lines that handle window activation, dead-pane closure, host pumping, ImGui frame lifecycle, dirty-rect rendering, host-specific draw, vsync wait, and shutdown transitions sequentially in a single function. This makes it hard to:
- Understand the frame budget at a glance
- Test individual stages
- Extend with new phases without the function growing further

Additionally, the ImGui host detection and rendering block (~20 lines) appears *identically* in both `App::pump_once()` and `App::run_render_test()`. Any change must be made in two places. Similarly, the ImGui font-size expression `static_cast<float>(text_service_.point_size() - 2)` (or equivalent) is duplicated three times in `app.cpp`.

---

## Implementation Plan

- [ ] Read `app/app.cpp` — map `pump_once()` into its logical stages.
- [ ] Read `App::run_render_test()` — identify the shared ImGui block.
- [ ] Extract private helper methods from `pump_once()`:
  - [ ] `close_dead_panes()` — removes panes whose host has exited.
  - [ ] `pump_hosts()` — iterates all hosts and calls their per-frame update.
  - [ ] `draw_host_imgui()` — the shared ImGui detection + draw block (replaces both `pump_once` and `run_render_test` copies).
  - [ ] `wait_for_vsync()` — the vsync/deadline wait at the end of the frame.
- [ ] Replace the duplicated ImGui font-size expression with a named private helper or `const` local variable.
- [ ] Verify `run_render_test()` now calls `draw_host_imgui()` instead of its copy.
- [ ] Build and run: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
- [ ] Run `clang-format` on all modified files.

---

## Acceptance

- `pump_once()` is ≤50 lines of stage calls plus the main state-machine switch.
- `draw_host_imgui()` is defined once and called from both `pump_once()` and `run_render_test()`.
- No behavior change — all smoke tests pass.

---

## Interdependencies

- `04-test` (app smoke test) — the decomposition makes individual stages easier to test in isolation.
- No upstream blockers.

---

*claude-sonnet-4-6*
