# Skip grids whose larger drawing allocation failed

**Summary:** Skip drawing when larger drawing memory cannot be created so a resize cannot read beyond the available storage.

**Priority:** 72  
**Severity:** CRITICAL  
**Source:** `libs/draxul-renderer/src/metal/metal_renderer.mm`

**Evidence and trigger:** B32; failed growth retains a smaller buffer, while drawing continues with the resized grid’s instance counts.

- [x] **Investigate:** Trace slot capacity, upload success, mapping, and draw counts after resize failure.
- [x] **Fix:** Return upload success and skip drawing until sufficient storage contains current state.
- [x] **Acceptance:** Injected growth and mapping failures emit no draw that addresses insufficient or stale storage; later frames recover.
- [x] **Validation:** Run core aggregate tests, same-cache smoke, and relevant macOS rendering checks.

## Investigation (2026-10-05)

- Confirmed still present: `MetalGridHandle::upload_state()` returned `void`. A failed
  `newBufferWithLength:` logged and returned with the previous, smaller slot buffer;
  a null `contents` returned without copying. `draw_grid_handle_now()` ignored both
  and drew `bg_instances()`/`fg_instances()` of the resized `RendererState` against
  that buffer — an out-of-bounds instanced read, or a draw of the slot's stale
  previous-frame cells after a mapping failure.
- Vulkan already returned upload success and skipped the draw on a failed growth.
  Its remaining equivalent gap: after a `Resized` buffer whose descriptor update
  failed (null descriptor sets after a failed swapchain-time rebuild), the next frame
  saw `Unchanged` and drew through descriptor sets that were never pointed at the
  live buffer.

## Fix

- New backend-private `libs/draxul-renderer/src/shared/grid_slot_upload.h`:
  `upload(state, storage)` grows/maps/copies one frame slot and returns true only when
  the slot holds the complete current state; `storage_covers_state()` checks the slot
  capacity against both the serialized state and every bg/fg instance index. Failure
  leaves the state dirty so the next frame retries.
- Metal: `MetalGridHandle::upload_state()` now returns that result through a
  `SlotStorage` adapter; `draw_grid_handle_now()` returns before encoding any draw
  when it is false.
- Vulkan: `VkGridHandle` tracks per-slot `descriptors_current_`, cleared on resize,
  invalidate, release, and retire; upload retries the descriptor update until it
  succeeds and refuses to draw a slot whose capacity does not cover the state.
  (Windows-only code; reviewed but not compiled on this macOS host.)

## Tests

- `tests/renderer_state_tests.cpp` `[grid-slot]`: a real `RendererState` driven through
  two frames-in-flight slots with injectable growth and mapping failures. Each
  permitted draw is checked against the slot's capacity and byte-for-byte contents;
  failed growth, mapping failure on unchanged capacity, and mapping failure of a new
  allocation all suppress the draw, and subsequent frames recover.

## Validation (2026-10-05, macOS)

- Focused `draxul-test-renderer "[grid-slot],[render]"`: 17 cases passed.
- Core aggregate `python3 do.py test debug`: 55/56 CTest entries passed in 43.9s. The
  one failure, `draxul-do-py-tests`, reads `plugins/megacity/product/AGENTS.md`, which
  is absent because the MegaCity submodule is not initialized in this worktree; it is
  unrelated to the renderer.
- Same-cache `python3 do.py smoke --skip-build` passed; `python3 do.py basic` passed
  (0.0636% changed pixels).
- Vulkan changes were reviewed but not compiled; Windows CI covers the build.
