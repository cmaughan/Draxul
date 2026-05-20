# 13 Atlas Overflow Retry Test

## Why This Exists

`GridRenderingPipeline::flush()` has a two-attempt retry loop that handles the case where the glyph atlas overflows mid-flush. On overflow: reset the atlas, mark all cells dirty, and re-run the flush. This is critical logic for long sessions or sessions with many distinct glyphs, but it has **zero test coverage**.

If this retry path is broken, the renderer will silently drop glyphs (blank cells) after the first atlas reset.

**Source:** `app/grid_rendering_pipeline.cpp` — the two-attempt retry block.
**Raised by:** Claude (primary), GPT, Gemini (all three flagged this gap).

## Goal

Add a unit test that drives the retry loop to verify:
1. When the atlas overflows during flush, the pipeline resets, re-marks all cells dirty, and retries.
2. After retry, glyphs that were present before overflow are re-uploaded and visible in the output.
3. The retry only happens once; a second overflow in the retry pass is handled without infinite looping.

## Implementation Plan

- [x] Read `app/grid_rendering_pipeline.cpp` and `app/grid_rendering_pipeline.h` to understand the retry logic and the interfaces used.
- [x] Read `tests/` to understand existing test patterns for the pipeline.
- [x] Identify or create a mock/stub `IGlyphAtlas` that can be configured to return "atlas full" on the first N upload attempts.
- [x] Write a test that:
  - Creates a small grid with distinct glyph clusters.
  - Uses a mock atlas configured to overflow on the first flush attempt.
  - Calls `flush()`.
  - Asserts that all cells were re-marked dirty after the overflow.
  - Asserts that the second attempt succeeds and produces a correct `CellUpdate` stream.
- [x] Add the test to the existing `draxul-tests` CMake target.
- [x] Run `ctest --test-dir build`.

## Tests

This entire work item is a test. The test should live in `tests/grid_rendering_pipeline_tests.cpp`
(create if it doesn't exist).

Implemented in `tests/grid_rendering_pipeline_tests.cpp` with a fake atlas and fake renderer that
cover both the successful single-retry path and the double-reset early-exit path.

## Sub-Agent Split

Single agent. The mock atlas may require reading `libs/draxul-types/include/draxul/glyph_atlas.h`
to understand the `IGlyphAtlas` interface.
