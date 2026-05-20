# WI 49 — hidpi-panel-hittest-double-scale

**Type**: bug  
**Priority**: 1 (wrong input routing on all Retina/HiDPI displays)  
**Source**: review-consensus.md §B2 [P][G][C]  
**Produced by**: claude-sonnet-4-6

---

## Problem

`PanelLayout::contains_panel_point()` (`libs/draxul-ui/include/draxul/ui_panel.h:51`) multiplies its input point by `pixel_scale`. However, `InputDispatcher` (`app/input_dispatcher.cpp:153`) already passes physical (already-scaled) coordinates to this function. On a Retina / 2× display the coordinates are scaled a second time, producing panel hit-tests that are off by a factor of 2 — mouse clicks near the diagnostics panel route to the wrong target.

This affects every macOS Retina display and every Windows HiDPI setup. The bug is latent on 1× displays.

## Investigation (2026-04-02)

**Bug confirmed via deep code trace.** The full coordinate flow:

1. **SDL3 mouse events** provide **logical** coordinates (`sdl_event_translator.cpp` lines 50, 65, 82)
2. **InputDispatcher** explicitly converts logical→physical via `deps_.pixel_scale.to_physical()` before calling `contains_panel_point()` (lines 153, 177, 200 — comment at lines 22-24 documents this intent)
3. **`contains_panel_point()`** then multiplies by `pixel_scale` again (lines 53-54 of `ui_panel.h`)
4. The bounds it checks against (`window_size`, `panel_y`, `panel_height`) are all in **physical pixels** (set from `window_->size_pixels()` in `app.cpp` lines 1096-1101)

**Result on 2× display:** coordinate `100` → `200` (physical) → `400` (double-scaled) — quadruple-scaled vs. the physical bounds. **On 1× display:** `100 × 1 × 1 = 100` — no visible effect, which is why it works on 1× laptops.

All three call sites (`on_mouse_button_event`, `on_mouse_move_event`, `on_mouse_wheel_event`) pass physical coordinates. No callers pass logical coordinates.

Existing unit tests (`tests/ui_panel_layout_tests.cpp` lines 43-45) implicitly assume logical coordinates, which masks the issue because the test's `pixel_scale` interaction cancels out.

**Recommended fix:** Remove the `pixel_scale` multiplication from `contains_panel_point()` and add a clear coordinate-space contract comment. The function should expect physical pixel coordinates (matching what all callers provide).

---

## Tasks

- [ ] Read `libs/draxul-ui/include/draxul/ui_panel.h` — understand the full `contains_panel_point()` contract and what coordinate space it documents.
- [ ] Read `libs/draxul-ui/src/ui_panel.cpp` — confirm how `pixel_scale` is stored and used throughout the file.
- [ ] Read `app/input_dispatcher.cpp` — find all call sites of `contains_panel_point()` (and similar panel-position queries) and confirm which coordinate space is passed.
- [ ] Decide the canonical fix: either (a) remove the `pixel_scale` multiplication from `contains_panel_point()` because callers always pass physical coordinates, or (b) define a logical-coordinate variant and update callers consistently. Document the contract clearly in a comment.
- [ ] Apply the fix to `ui_panel.h/cpp` and the call sites in `InputDispatcher`.
- [ ] Verify no other callers of the panel hit-test function exist that pass logical coordinates (grep for `contains_panel_point`).
- [ ] Build and run smoke test: `cmake --build build --target draxul draxul-tests && py do.py smoke`

---

## Acceptance Criteria

- `contains_panel_point()` has an unambiguous documented coordinate contract.
- On a 2× display (or with `pixel_scale=2` in a test), mouse clicks at the panel boundary route to the correct target.
- No other callers pass the wrong coordinate space.
- Smoke test passes.

---

## Interdependencies

- **WI 52** (hidpi-panel-hittest-unit-tests) — unit test for this fix; file both in the same agent pass.

---

## Notes for Agent

- Read the actual files before deciding the fix direction; the contract may already be documented somewhere.
- Avoid changing the `ui_panel.h` public interface unless necessary; prefer fixing the multiplication site.
- A sub-agent is not needed; this is a focused 2–3 file change.
