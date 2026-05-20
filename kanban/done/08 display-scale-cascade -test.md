# 08 display-scale-cascade — Test

## Summary

`App::on_display_scale_changed()` in `app/app.cpp` must trigger a cascade of operations when the display DPI changes (e.g., moving the window to a HiDPI monitor):

1. `TextService` reinitializes with the new PPI.
2. Renderer cell size is updated.
3. Ascender value is updated.
4. ImGui font is rebuilt.
5. Font texture is regenerated.
6. `on_font_metrics_changed()` is called on the current host.
7. Viewport is recalculated and refreshed.

This cascade is currently untested. Additionally, the same 6–7 step sequence is duplicated verbatim in three places in `app.cpp` (see companion refactor item 11 — `font-metrics-rebuild-dedup`). If any one copy diverges, DPI changes silently produce wrong metrics. This test should ideally be written after (or alongside) item 11's dedup refactor so there is a single, testable code path.

## Steps

- [ ] 1. Read `app/app.cpp` in full to find `on_display_scale_changed()` and understand the full cascade sequence. Note every subsystem call in order.
- [ ] 2. Read `app/app.h` to understand what dependencies `App` holds (renderer, text service, host, window).
- [ ] 3. Read `tests/support/test_support.h` and any existing app-level test for the pattern of constructing a testable `App`-like object or wiring fakes together.
- [ ] 4. Determine whether `App::on_display_scale_changed()` can be called in a test:
  - If `App` is directly constructable with fake dependencies, use it.
  - If not, extract the cascade sequence into a helper (see item 11) and test the helper directly.
- [ ] 5. Create or update `tests/display_scale_tests.cpp`. Build a test harness:
  - `FakeWindow` that reports a configurable DPI/scale factor (use or extend the shared fake from item 10, or create a local one).
  - `FakeRenderer` that records calls to `set_cell_size()`, `set_viewport()`, etc.
  - `FakeTextService` (or real TextService if constructable without fonts) that records reinit calls.
  - `FakeHost` that records `on_font_metrics_changed()` calls.

  **Test 1: DPI change triggers full cascade**
  - Construct `App` (or the cascade helper) with fakes.
  - Initial DPI = 96.
  - Call `on_display_scale_changed(144)` (1.5× DPI change).
  - Assert: `FakeTextService` was reinitialized (reinit call count == 1).
  - Assert: `FakeRenderer::set_cell_size()` was called with the new cell dimensions.
  - Assert: `FakeHost::on_font_metrics_changed()` was called exactly once.
  - Assert: `FakeRenderer::set_viewport()` was called (layout refreshed).

  **Test 2: DPI change to same value is idempotent (no spurious cascade)**
  - Call `on_display_scale_changed(96)` when already at 96 DPI.
  - Assert: either no cascade occurs, or if it does occur (acceptable), all steps run without error.

  **Test 3: Cascade uses new DPI value, not stale**
  - Set DPI = 96. Call cascade. Record cell_size_1.
  - Set DPI = 192. Call cascade. Record cell_size_2.
  - Assert: cell_size_2 == 2 × cell_size_1 (or proportionally correct per the font metrics).

- [ ] 6. Register the new test file in `tests/CMakeLists.txt`.
- [ ] 7. Register test cases in `tests/test_main.cpp` if manual registration is used.
- [ ] 8. Build: `cmake --build build --target draxul-tests`. Confirm no compile errors.
- [ ] 9. Run: `ctest --test-dir build -R draxul-tests`. All tests must pass.
- [ ] 10. Run clang-format on all touched files.
- [ ] 11. Mark this work item complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- A test verifies the full DPI-change cascade: TextService reinit, renderer cell size update, host metrics callback, viewport refresh.
- The test fails if any cascade step is removed from `on_display_scale_changed()`.
- All existing tests continue to pass.

## Note

If item 11 (font-metrics-rebuild-dedup) has not been completed first, write this test against the current structure and note that it will need updating after the dedup refactor consolidates the three code paths.

*Authored by: claude-sonnet-4-6*
