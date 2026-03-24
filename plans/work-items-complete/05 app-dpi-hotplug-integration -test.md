# 05 app-dpi-hotplug-integration -test

**Type:** test
**Priority:** 5
**Source:** GPT review (review-latest.gpt.md); Gemini (DPI transition simulation)

## Problem

`dpi_scaling_tests.cpp` verifies formulas (`96 * scale`, `compute_panel_layout`) and `TextService` reinit. It does NOT cover `App::on_display_scale_changed()`, which coordinates:
1. Font rebuild (new point size)
2. ImGui texture rebuild
3. Viewport recompute
4. `InputDispatcher` pixel-scale sync

A regression in any of these four coordination steps would not be caught by existing tests. GPT explicitly called this out: "DPI behavior is tested as math more than as application behavior."

## Acceptance Criteria

- [x] Read `app/app.cpp` around `on_display_scale_changed()` and the existing `tests/dpi_scaling_tests.cpp`.
- [x] Use the testable `App` harness (with `FakeWindow` + `FakeRenderer`) to simulate a display-scale change event.
- [x] Add tests that verify, after triggering a scale-change event:
  - [x] `TextService` (or the font metrics) reflects the new cell size.
  - [x] The renderer grid handle's buffer dimensions match the new cell geometry.
  - [x] `InputDispatcher`'s pixel-scale value is updated (check via a subsequent mouse event landing on the expected cell).
  - [x] No crash or assertion failure occurs when the event fires mid-frame (i.e., with a partially rendered frame in progress if the harness supports this).
- [x] Run under `ctest` and `mac-asan`.

## Implementation Notes

- This may require extending `FakeWindow` to emit a `DisplayScaleEvent` if it does not already. Check the existing window test infrastructure first.
- If `App::on_display_scale_changed()` is not directly callable from tests, look for a `pump_event()` or `handle_event()` path that accepts window events.
- A sub-agent is well-suited for this: explore the existing DPI test harness, understand the event flow, then write the orchestration test.

## Interdependencies

- No blockers. Can be worked in parallel with all other test items.
- Provides safety net for icebox 19 (per-monitor DPI font scaling).

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
