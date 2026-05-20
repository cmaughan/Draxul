# 25 renderer-backend-parity-cleanup -refactor

**Priority:** HIGH
**Type:** Refactor
**Raised by:** Codex (Metal/Vulkan parity review)
**Model:** gpt-5

---

## Summary

Metal and Vulkan are now much closer at the public renderer boundary: both support two frames in flight, shared `IRenderPass` composition, ImGui hosting, frame capture, depth-backed 3D passes, and per-frame Megacity scene resources.

The remaining gaps are mostly implementation-quality and backend-shape issues rather than missing top-level features. Vulkan is still the more mature reference backend: it has a cleaner explicit resource model, staged atlas uploads, and more deliberate swapchain/resource lifecycle handling. Metal still has a simpler but less structured implementation, and there are a few backend-specific cleanup opportunities on both sides.

This work item is a ranked cleanup list for closing the most important remaining parity gaps without forcing artificial symmetry where the APIs genuinely differ.

---

## Ranked Cleanup List

### Rank 1: Fix the outstanding Metal render drift

- [ ] Investigate and fix the existing `draxul-render-panel-view` snapshot drift on Metal.
- [ ] Confirm whether the drift is caused by the newer multi-frame grid path, an older panel/ImGui composition difference, or a pre-existing Metal-only rendering mismatch.
- [ ] Keep the fix minimal and specific; do not mix it with larger renderer cleanup.

Why first:
- It is the only currently visible parity failure in validation.
- It blocks confidence in the recent Metal renderer changes.

---

### Rank 2: Give Metal a real atlas upload path

- [x] Replace direct `replaceRegion()` atlas writes in `libs/draxul-renderer/src/metal/metal_renderer.mm` with a more deliberate upload path.
- [x] Decide whether that should be:
  1. a simple shared upload buffer plus blit encoder, or
  2. a small Metal atlas helper mirroring the role of `VkAtlas`.
  **Decision:** Approach 1 — shared staging buffer + blit encoder. Both backends now use the same deferred `PendingAtlasUpload` queue (shared header `pending_atlas_upload.h`), with Metal using per-frame `MTLBuffer` staging + `MTLBlitCommandEncoder`, and Vulkan using its existing `VkAtlas` staging buffer + `vkCmdCopyBufferToImage`.
- [x] Preserve the existing partial-region update API.

Why second:
- Vulkan already has a proper staged atlas upload implementation in `libs/draxul-renderer/src/vulkan/vk_atlas.cpp`.
- Atlas uploads are the clearest remaining place where Metal still looks like the simpler backend rather than a peer implementation.

---

### Rank 3: Remove the stale legacy Vulkan renderer state

- [x] Remove or isolate the old single-handle `RendererState` path still living in `VkRenderer`.
- [x] Keep `VkGridHandle` plus per-frame repacking as the single active grid path.
- [x] Delete dead helper methods or move them behind a clearly-marked compatibility layer if anything still depends on them.
  **Done:** Removed `RendererState state_` member and 5 dead legacy methods (`set_grid_size`, `update_cells`, `set_overlay_cells`, `set_cursor`, `set_state_background`) from VkRenderer. Removed `state_.*` calls from `set_cell_size`/`set_ascender`.

Why third:
- Vulkan currently carries both the modern multi-pane handle path and older renderer-state helpers.
- That makes Vulkan harder to reason about and obscures the real parity comparison with Metal.

---

### Rank 4: Reclaim efficient partial grid uploads

- [x] Re-evaluate grid upload strategy now that both backends are frame-buffered.
- [x] Decide whether to:
  1. keep full per-frame copies as the simplicity baseline, or
  2. reintroduce dirty-span-aware uploads on top of the per-frame slot model.
  **Decision:** Keep full per-frame copies as the simplicity baseline. Both backends now use identical single-buffer packing: renderer owns per-frame shared buffers, all handles are packed sequentially, drawn with instance offsets (`firstInstance` on Vulkan, `baseInstance:` on Metal).
- [ ] If dirty uploads are restored, implement them consistently enough that one backend does not become the only optimized path.

Why fourth:
- `RendererState` still tracks dirty ranges, but both backends currently copy full state into the active frame slot.
- This is not a correctness bug, but it leaves performance on the table and makes the dirty-tracking machinery underused.

---

### Rank 5: Clarify transient geometry policy for dynamic scene content

- [ ] Keep Megacity’s floor grid on the transient-buffer path when it is derived from dynamic visible extents.
- [ ] Document the intended rule: dynamic scene geometry should stream through transient buffers; static scene geometry may use persistent GPU buffers.
- [ ] Make sure future Megacity geometry work follows that split deliberately rather than optimizing away the transient path by default.

Why fifth:
- The current floor-grid behavior matches the intended long-term model for dynamic geometry.
- What is still missing is an explicit backend policy so future work does not drift toward the wrong upload model.

---

### Rank 6: Split the Metal renderer into clearer internal helpers

- [ ] Break `libs/draxul-renderer/src/metal/metal_renderer.mm` into smaller backend-private units or helper types where it improves clarity.
- [x] Likely split areas:
  - atlas upload/state — extracted into `flush_pending_atlas_uploads()` with per-frame staging buffers (matches VkAtlas pattern)
  - grid buffer management — moved from per-handle ownership to renderer-owned shared buffers with `upload_dirty_state()` packing (matches VkRenderer pattern)
  - frame capture
  - frame submission / drawable lifecycle
- [ ] Preserve the existing public boundary; this is an internal cleanup only.

Why sixth:
- Vulkan already benefits from `VkContext`, `VkAtlas`, `VkPipelineManager`, and `VkGridBuffer`.
- Metal is still much more monolithic, which makes parity work and debugging less local than it should be.

---

### Rank 7: Reduce Vulkan’s conservative atlas-upload fence waits

- [x] Review `VkRenderer::flush_pending_atlas_uploads()` and replace the current “wait for all other in-flight frames” behavior with something narrower if possible.
- [x] Keep correctness first; do not introduce atlas hazards just to shave a fence wait.
  **Done:** Changed `VkAtlas` to own per-frame staging buffers (`staging_[MAX_FRAMES_IN_FLIGHT]`), matching Metal’s per-frame staging model. Removed the cross-frame fence waits from `flush_pending_atlas_uploads()` — each frame slot’s fence (waited on in `begin_frame`) now guarantees its staging buffer is safe to reuse.

Why seventh:
- Vulkan’s atlas path is structurally stronger than Metal’s, but it is also conservative and may stall more than necessary.
- This is a backend polish issue, not a correctness gap.

---

### Rank 8: Revisit static mesh memory placement for 3D scene content

- [ ] Review whether Megacity static meshes should stay in CPU-visible/shared buffers or move toward GPU-preferred/private storage with an upload path.
- [ ] Apply only if profiling or scene growth justifies the added complexity.
- [ ] Avoid doing this just for symmetry; it should be motivated by real renderer pressure.

Why last:
- Both backends currently work correctly with the simpler approach.
- This is a scalability refinement, not a present correctness or parity problem.

---

## Implementation Notes

- Treat Vulkan as the current structural reference backend, but do not force Metal into Vulkan-shaped abstractions where Metal has a simpler native path.
- Prefer fixing visible validation failures before refactoring internal shape.
- Keep backend-private code backend-private; avoid pushing platform-specific implementation details into public renderer headers unless both backends need the same concept.
- If a cleanup deletes stale code on only one backend, that still counts as parity improvement if it makes the two paths easier to compare and maintain.
- For Megacity and future scene work, transient buffers are the preferred model for dynamic geometry updates. Persistent GPU mesh allocation should be reserved for genuinely static geometry.

---

## Acceptance Criteria

- [ ] The Metal backend passes the same baseline render validation as Vulkan for the supported scenario set, including `panel-view`.
- [ ] The biggest remaining backend differences are intentional API/platform differences, not obvious maturity gaps.
- [ ] Atlas uploads, grid uploads, Megacity scene resources, and render-pass composition all have a clear documented “why it differs” story where the backends are not identical.
- [ ] The renderer code is easier to reason about locally, with less stale compatibility logic and fewer hidden backend-specific special cases.
