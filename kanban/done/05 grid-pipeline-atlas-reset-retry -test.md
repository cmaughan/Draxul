# 05 grid-pipeline-atlas-reset-retry — Test

## Summary

`GridRenderingPipeline::flush()` in `libs/draxul-app-support/src/grid_rendering_pipeline.cpp` contains a retry loop (up to 2 attempts) that handles the case where the glyph atlas fills up and resets mid-flush. The logic is approximately:

```
for attempt in 0..2:
    for each cell:
        result = atlas.get_or_rasterize(glyph)
        if result == ATLAS_RESET:
            if attempt == 0: break and retry
            else: silently continue (stale UV risk)
    if no reset occurred: return success
```

The second-attempt path (`attempt > 0`) has never been exercised by the test suite. Additionally, if the atlas resets twice within a single flush (all cells in the retry attempt also overflow), the second reset is silently ignored, and the GPU may receive stale UV coordinates pointing to newly-overwritten atlas tiles.

## Steps

- [ ] 1. Read `libs/draxul-app-support/src/grid_rendering_pipeline.cpp` in full to understand the retry loop structure, the atlas interaction, and how cell UV coordinates are written to the GPU buffer.
- [ ] 2. Read `libs/draxul-app-support/include/draxul/` for `GridRenderingPipeline` interface and any `IGlyphAtlas` or atlas-related types.
- [ ] 3. Read `tests/CMakeLists.txt` and `tests/test_main.cpp` for test registration patterns.
- [ ] 4. Identify how to construct `GridRenderingPipeline` in a test. Determine what fake/mock implementations of the renderer and atlas are needed. Check `tests/support/` for existing fakes.
- [ ] 5. Create `tests/grid_pipeline_atlas_retry_tests.cpp` (or add to an existing relevant test file).

  **Test 1: Single atlas reset triggers retry and succeeds**
  - Configure a `FakeAtlas` that returns `ATLAS_RESET` on the very first call to `get_or_rasterize()`, then succeeds on all subsequent calls.
  - Build a grid row with 3 distinct glyphs.
  - Call `flush()`.
  - Assert: flush returns success.
  - Assert: all 3 cells have valid (non-stale) UV coordinates in the GPU buffer (UV values correspond to the retry-phase rasterization, not the aborted first attempt).

  **Test 2: Atlas reset on every attempt (double reset)**
  - Configure a `FakeAtlas` that always returns `ATLAS_RESET` on the first call of any flush attempt (i.e., resets on attempt 0 AND on attempt 1).
  - Build a grid row with enough distinct glyphs to trigger atlas overflow on each attempt.
  - Call `flush()`.
  - Assert: flush does NOT crash.
  - Assert: either (a) a warning/log is emitted, OR (b) the cells have been written with coordinates that are at minimum consistent (no UB, no wild pointer offsets). Document which behavior is currently implemented.

  **Test 3: Normal path (no reset) still works**
  - Configure a `FakeAtlas` that always succeeds.
  - Call `flush()` with a 4-cell row.
  - Assert: first-attempt path completes, no retry, all UVs valid.

- [ ] 6. If `FakeAtlas` does not exist, create `tests/support/fake_atlas.h` with a configurable return value (success vs. ATLAS_RESET).
- [ ] 7. Register the new test file in `tests/CMakeLists.txt`.
- [ ] 8. Register test cases in `tests/test_main.cpp` if manual registration is used.
- [ ] 9. Build: `cmake --build build --target draxul-tests`. Confirm no compile errors.
- [ ] 10. Run: `ctest --test-dir build -R draxul-tests`. All tests must pass.
- [ ] 11. If test 2 reveals an actual bug (stale UVs with no log warning), file the bug as a follow-up note in this work item before marking complete.
- [ ] 12. Run clang-format on all new/touched files.
- [ ] 13. Mark this work item complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- The retry path (attempt > 0) is exercised by at least one test.
- The double-reset behavior is documented (either tested as correct or identified as a latent bug).
- The normal no-reset path continues to pass.
- All existing tests continue to pass.

*Authored by: claude-sonnet-4-6*
