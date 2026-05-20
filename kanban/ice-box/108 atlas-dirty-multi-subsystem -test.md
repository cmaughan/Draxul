# WI 108 — atlas-dirty-multi-subsystem

**Type:** test
**Priority:** 5 (test — validates correctness of WI 109 refactor and guards against atlas ordering bugs)
**Source:** review-consensus.md §3 [GPT]
**Produced by:** claude-sonnet-4-6

---

## Problem / Gap

Three independent subsystems (grid pipeline, chrome host, command palette host) each inspect and clear the shared glyph-atlas dirty state. There is no test verifying that:

1. When multiple subsystems all resolve new glyphs in the same frame, the atlas is uploaded exactly once and all subsystems see the updated atlas.
2. When the atlas is clean, no redundant uploads are triggered.
3. When the atlas overflows and resets, all subsystems handle the reset correctly.

Without this test, the refactor in WI 109 (atlas-upload-dedup) has no regression guard.

---

## What to Test

1. **Single-frame multi-subsystem upload:** Drive the grid pipeline, chrome host, and command palette host in a single simulated frame where each has new glyph data. Assert the atlas upload path fires exactly once; assert all subsystems receive the updated UV coordinates.
2. **Clean frame — no upload:** Drive a frame with no new glyphs. Assert no atlas upload occurs.
3. **Atlas overflow / reset:** Fill the atlas to capacity; assert the overflow path is triggered; assert all subsystems that had cached UVs invalidate them correctly.

---

## Implementation

- [ ] Use the fake/headless renderer path available in `tests/support/` to drive frame simulation.
- [ ] Find the render-pipeline test or scenario test that exercises atlas interaction (likely in `tests/grid_rendering_pipeline_tests.cpp` or similar).
- [ ] Add the multi-subsystem scenario as a new test case.
- [ ] Build: `cmake --build build --target draxul-tests`
- [ ] Run: `ctest --test-dir build -R atlas`

---

## Acceptance Criteria

- [ ] Three test cases added (multi-subsystem upload, clean frame, overflow reset).
- [ ] Tests pass with the WI 109 refactored code path.
- [ ] Tests **fail** if the upload is called redundantly (upload-count assertion).

---

## Interdependencies

- **WI 109** (atlas-upload-dedup -refactor) — write this test alongside or after the refactor to validate it.
