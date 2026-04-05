# WI 87 — metal-grid-handle-uaf-shutdown

**Type:** bug  
**Priority:** 1 (use-after-free on abnormal shutdown)  
**Source:** review-bugs-consensus.md §H1 [Claude]  
**Produced by:** claude-sonnet-4-6

---

## Problem

`MetalGridHandle` (`libs/draxul-renderer/src/metal/metal_renderer.mm:37–51`) stores `MetalRenderer& renderer_` as a raw reference. If a host-owned `unique_ptr<IGridHandle>` outlives the renderer — possible when cleanup order is unspecified during abnormal shutdown — the handle destructor at line 47 dereferences `renderer_.grid_handles_` through a dangling reference. `VkGridHandle` has the same pattern.

Under AddressSanitizer this manifests as a heap-use-after-free immediately on shutdown; in production it is a silent memory stomp.

---

## Investigation

- [ ] Read `libs/draxul-renderer/src/metal/metal_renderer.mm:37–51` — confirm the raw reference and the destructor path.
- [ ] Read `MetalRenderer::shutdown()` — check whether `grid_handles_` is cleared before GPU resources are released; if not, confirm the destruction gap.
- [ ] Read `libs/draxul-renderer/src/vulkan/vk_renderer.cpp` — locate `VkGridHandle` and confirm the identical pattern.
- [ ] Grep for `create_grid_handle` call sites — identify all owners of `IGridHandle` and their lifetimes relative to the renderer.

---

## Fix Strategy

- [ ] In `MetalRenderer::shutdown()` (before releasing GPU resources), iterate `grid_handles_`, call a `retire()` or `invalidate()` method on each that nulls the back-reference, then `grid_handles_.clear()`.
- [ ] Apply the same fix to `VkRenderer::shutdown()`.
- [ ] Alternatively, store a `std::weak_ptr<MetalRenderer>` (requires the renderer to be owned by `shared_ptr`) — only use this if ownership already uses `shared_ptr`.
- [ ] Confirm that the handle destructor is a no-op (or safe) after `retire()`/`invalidate()`.
- [ ] Build with ASan: `cmake --preset mac-asan && cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] No use-after-free reported by AddressSanitizer during normal or abnormal shutdown.
- [ ] `MetalRenderer::shutdown()` and `VkRenderer::shutdown()` clear all handles before releasing GPU resources.
- [ ] Smoke test passes.

---

## Interdependencies

- **WI 85** (metal-capture-semaphore-race) — both modify `metal_renderer.mm`; combine into one Metal patch to minimise conflicts.
- **WI 48** (vk-null-grid-handle-dereference) — audits `VkRenderer::create_grid_handle` call sites; coordinate renderer shutdown changes.
