# NanoVG Vulkan resource synchronization

**Type:** bug
**Priority:** 04
**Raised by:** Claude; supported by GPT/Codex's GPU-contract review

## Problem

`libs/draxul-nanovg/src/nanovg_vk.cpp` advertises sampled images as `SHADER_READ_ONLY_OPTIMAL` without a complete tracked transition path, transitions the dummy texture from `UNDEFINED` repeatedly, maps allocations without an explicit coherency/flush policy, and hardcodes three frames in flight while the renderer reports two.

## Implementation plan

- [ ] Inventory every NanoVG image's created layout, upload path, last writer, descriptor layout, and destruction point.
- [ ] Track actual layouts and issue transfer-to-sampled barriers with correct stage/access masks; transition each image once per real state change.
- [ ] Stop using `UNDEFINED` after the dummy texture contains meaningful pixels.
- [ ] Select coherent host memory or call `vmaFlushAllocation` for every non-coherent mapped write.
- [ ] Size per-frame resources from `IRenderContext::buffered_frame_count()` and rebuild safely if that contract changes.
- [ ] Add validation-layer names/log context for NanoVG images, buffers, descriptors, and barriers.
- [ ] While in the file, factor repeated pipeline creation only where it reduces the chance of inconsistent layouts; avoid an unrelated rewrite.
- [ ] Replace the floating NanoVG `master` dependency with a reviewed immutable commit/tag and record any local backend patches.

## Tests

- [ ] Add a validation-enabled NanoVG smoke rendering textured and untextured paths across more frames than the buffered count.
- [ ] Exercise texture create/update/delete and swapchain recreation.
- [ ] Verify the dummy texture retains data across flushes.

## Acceptance criteria

- [ ] Vulkan validation reports no NanoVG layout, access, or frame-slot errors.
- [ ] No meaningful texture is transitioned from `UNDEFINED` after initialization.
- [ ] NanoVG render reference remains stable on Windows.
- [ ] Build renderer/app, run focused tests, render scenario, `ctest`, and smoke.

## Dependencies and parallelism

Fix before item 28 so shared helpers do not encode incorrect behavior. Best owned by one Vulkan-focused agent.

<model>GPT-5 Codex</model>
