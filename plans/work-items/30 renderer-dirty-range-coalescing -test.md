# 30 renderer-dirty-range-coalescing -test

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.claude.md [C]*

## Problem

`renderer_state.cpp` tracks dirty ranges for incremental GPU buffer upload.  The following
behaviours are untested:

1. **Overlapping dirty regions** — two writes that overlap should coalesce into a single
   minimal upload range, not two separate (potentially redundant or double-counted) ranges.
2. **Adjacent dirty regions** — two writes that are contiguous should coalesce rather than
   producing a gap-free but fragmented range list.
3. **Cursor save/restore** — a cursor move (which invalidates one cell) should dirty only that
   cell, not the entire buffer.  Save/restore of the cursor should not cause a full-buffer
   re-upload on restore.
4. **Overlay interaction** — an overlay region written on top of a previously dirtied region
   should not expand the dirty range beyond what is needed.

Without these tests, a future change to the coalescing algorithm could silently introduce
unnecessary full-buffer uploads (a performance regression) or missed uploads (a correctness
regression).

## Acceptance Criteria

- [ ] Tests for scenarios 1–4 above exist in `tests/renderer_state_tests.cpp` (new or added
      to existing renderer tests).
- [ ] A cursor-move test verifies the dirty range covers exactly one cell, not the whole
      buffer.
- [ ] A coalescing test verifies overlapping ranges produce a single merged range.
- [ ] All tests pass under `ctest`.

## Implementation Plan

1. Read `libs/draxul-runtime-support/include/draxul/renderer_state.h` (or wherever
   `RendererState` lives) and `renderer_state.cpp` to understand the dirty range API.
2. Identify the test seam — can `RendererState` be constructed without a real GPU context?
   If not, use the fake renderer infrastructure.
3. Write unit tests exercising each scenario with direct calls to the dirty-tracking API.
4. Run `cmake --build build --target draxul-tests && ctest -R renderer_state`.

## Files Likely Touched

- `tests/renderer_state_tests.cpp` (new or extended)
- `tests/CMakeLists.txt` if a new file

## Interdependencies

- **WI 41** (`cmake-configure-depends`) should land first if creating a new test file.
- Independent of other open WIs at the implementation level.
