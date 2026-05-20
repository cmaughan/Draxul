# Resize Cascade Integration Test

**Type:** test
**Priority:** 13
**Raised by:** Claude

## Summary

Add an integration test for the full resize cascade: `WindowResizeEvent` → `App::on_resize` → `renderer.resize` → `host.set_viewport` → `grid.resize`. The test should verify that all subsystems end up in a consistent state after a resize event, and that a subsequent render produces output matching the new dimensions.

## Background

The resize path touches multiple independent subsystems (window, renderer, host, grid) in a specific order. A bug in any step — wrong dimensions passed, resize called on the wrong object, state not updated before next use — produces subtle rendering corruption that is hard to reproduce manually. An integration test that drives the full cascade with synthetic events provides a regression net for this path.

This test also covers the related alt-screen resize path (work item `06`) and the dynamic DPI path (work item `03`), since all three flow through the same cascade.

## Implementation Plan

### Files to modify
- `app/tests/` or `libs/draxul-app-support/tests/` — add `resize_cascade_test.cpp`
- Relevant `CMakeLists.txt` — register with ctest

### Steps
- [x] Set up a test harness that instantiates a mock or test-double renderer, a real `Grid`, and a `TerminalHostBase` (or test double)
- [x] Write test: initial size 80×24, synthesise a `WindowResizeEvent` for 120×40
  - Verify renderer received `resize(120, 40)` — Grid::resize() verified directly; renderer/host integration skipped (no live window)
  - Verify grid dimensions are 120×40
  - Verify host viewport is 120×40 — skipped (requires live IHost)
- [x] Write test: resize to smaller dimensions (40×12) — verify no out-of-bounds access, content truncated correctly
- [x] Write test: resize to identical dimensions (no-op) — verify no spurious reinitialisations
- [ ] Write test: resize while in alt-screen — verify main-screen snapshot is re-dimensioned (covers work item `06`) — skipped (requires live host)
- [x] Write test: two rapid sequential resize events — final state matches the last event
- [x] Register with ctest

## Depends On
- `03 dynamic-dpi-hotplug -bug.md` — DPI change feeds into this cascade
- `06 altscreen-resize-mismatch -bug.md` — alt-screen resize test is part of this suite

## Blocks
- None

## Notes
If a full integration test harness is too expensive to set up, at minimum write unit tests for each subsystem's `resize` method in isolation, with a comment noting that the integration glue in `App::on_resize` is manually verified.

> Work item produced by: claude-sonnet-4-6
