# 14 Block Cursor Save/Restore Test

## Why This Exists

`RendererState` saves the cell under the block cursor into `cursor_saved_cell_` when the cursor is applied, and restores it when the cursor moves or is hidden. This save/restore path is load-bearing — without it, cell content under the cursor is permanently overwritten.

The non-block (overlay) cursor path is tested, but the block cursor save/restore path through `cursor_saved_cell_` and the `memcmp` guard (see work item 10) is not covered by any existing test.

**Source:** `libs/draxul-renderer/src/renderer_state.cpp`.
**Raised by:** Claude.

## Goal

Add focused tests for the block cursor save/restore path:
1. Apply block cursor at a cell → verify the cell content is saved.
2. Move cursor to a different cell → verify the original cell content is restored.
3. Update an adjacent cell while the cursor is applied → verify the updated cell is unchanged after restore.
4. Hide the cursor → verify the saved cell is restored.

## Implementation Plan

- [x] Read `libs/draxul-renderer/src/renderer_state.h` and `renderer_state.cpp` to understand how cursor application and restore work.
- [x] Read the existing renderer state tests to understand test patterns in use.
- [x] Write test cases:
  - `block_cursor_saves_cell_under_cursor`
  - `block_cursor_restores_on_move`
  - `block_cursor_restores_on_hide`
  - `adjacent_cell_update_does_not_corrupt_cursor_restore`
- [x] Add tests to the existing `draxul-tests` target (likely `tests/renderer_state_tests.cpp`).
- [x] Run `ctest --test-dir build`.

## Notes

This test is complementary to work item 10 (fixing the `memcmp` UB). Both should be done in
the same or adjacent commits so the fix is validated by the new test.

Implemented in `tests/renderer_state_tests.cpp` with focused coverage for save, move, hide, and
adjacent-update restore behavior.

## Sub-Agent Split

Single agent. All test code lives in `tests/renderer_state_tests.cpp`.
