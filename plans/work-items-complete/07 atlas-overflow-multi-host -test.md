# 07 atlas-overflow-multi-host -test

**Type:** test
**Priority:** 7
**Source:** Claude review (review-latest.claude.md)

## Problem

When the glyph atlas overflows and resets, all glyphs must be re-rasterised. The concern is that cells belonging to a *second* `IGridHandle` (a second pane/host) should still be marked dirty and re-uploaded after the reset. There is no test verifying this behaviour — a regression here would cause one pane's glyphs to appear blank or garbled after atlas overflow while the other pane renders correctly.

## Acceptance Criteria

- [x] Locate the atlas reset logic in `libs/draxul-renderer/` (likely in the `GridRenderingPipeline` or atlas management code).
- [x] Read the existing `05 grid-pipeline-atlas-reset-retry -test.md` (complete) to understand the existing coverage.
- [x] Add a test that:
  - [x] Creates (or simulates) two `IGridHandle` instances backed by the same atlas.
  - [x] Fills the atlas to overflow.
  - [x] Triggers the reset.
  - [x] Verifies that cells from both handles are marked dirty and produce correct re-uploads (check dirty flags or re-rasterisation counts).
- [ ] Run under `ctest` and `mac-asan`.

## Implementation Notes

- If the renderer is hard to unit-test directly, use the `FakeRenderer` path and exercise the `GridRenderingPipeline` layer.
- A sub-agent is appropriate: read the atlas/pipeline code and existing tests, then determine the right injection point.

## Interdependencies

- No blockers. Independent test item.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
