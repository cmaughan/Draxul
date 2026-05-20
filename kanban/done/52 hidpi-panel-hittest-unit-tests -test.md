# WI 52 — hidpi-panel-hittest-unit-tests

**Type**: test  
**Priority**: 4 (regression guard for WI 49)  
**Source**: review-consensus.md §T1 [P][G][C]  
**Produced by**: claude-sonnet-4-6

---

## Problem / Goal

After WI 49 (hidpi-panel-hittest-double-scale) is fixed, there is no automated guard preventing the double-scale regression from returning. This item adds focused unit tests for the coordinate contract.

---

## Pre-condition

**WI 49 must be merged before writing these tests**, so the tests validate the correct post-fix behaviour rather than the broken pre-fix state.

---

## Tasks

- [x] Read `libs/draxul-ui/include/draxul/ui_panel.h` and `libs/draxul-ui/src/ui_panel.cpp` — understand the complete `PanelLayout` struct and `contains_panel_point()` signature.
- [x] Read existing `tests/` for any panel or UI panel tests (search for `ui_panel` or `PanelLayout`) to pick the right test file to extend or create alongside.
- [x] Write a test: `PanelLayout` at `pixel_scale=1.0` — confirm that a point inside the panel's logical rect returns true and a point outside returns false. (`hittest_boundaries_1x_scale`)
- [x] Write a test: `PanelLayout` at `pixel_scale=2.0` — confirm the same boundary behaviour with physical (doubled) coordinates; this is the regression test for WI 49. (`hittest_boundaries_2x_scale`)
- [x] Write a hidden-layout regression test (`hidden_panel_never_matches`) covering both 1× and 2× scales — used in place of the heavier integration test, since the unit-level boundary tests already lock in the WI 49 contract.
- [x] Build and run: `cmake --build build --target draxul-tests && ctest --test-dir build -R draxul-tests`

---

## Acceptance Criteria

- [x] At least 3 new test cases covering 1× and 2× pixel scale boundary conditions.
- [x] All tests pass on `mac-debug` preset.
- [x] No existing test regressions.

---

## Interdependencies

- **Requires WI 49** merged first.
- No other dependencies.

---

## Notes for Agent

- Use the existing `tests/support/fake_renderer.h` and `tests/support/fake_grid_pipeline_renderer.h` as the injection model.
- Keep tests in the `tests/` directory following existing naming conventions (`*_tests.cpp`).
- Do not test internal ImGui layout logic; test only the `PanelLayout` coordinate contract.
