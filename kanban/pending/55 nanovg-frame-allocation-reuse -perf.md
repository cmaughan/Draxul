# Reuse NanoVG native frame allocations

**Source:** `libs/draxul-nanovg/backend/src/nanovg_vk.cpp`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude. Vulkan lines 1442–1461, 1500–1539, and 1616–1633 allocate buffers/framebuffer/descriptor state on flush; Metal `nanovg_mtl.mm:795–816` creates two buffers for each nonempty flush. Chrome and ScoreView exercise these paths repeatedly.

- [ ] **Baseline:** Count native allocations, flushes, and frame CPU after warmup in chrome and a long ScoreView Flow.
- [ ] **Implement:** Use per-slot growable arenas supporting multiple flushes in one frame; cache compatible framebuffer/descriptor state.
- [ ] **Functional safety:** Preserve completion retirement, resize invalidation, aborted flushes, and no overwrite of in-flight data.
- [ ] **Compare:** Require warm steady frames to avoid repeated native object creation where capacity suffices.
- [ ] **Platforms:** Check Vulkan and Metal NanoVG snapshots, aggregate and smoke.
- [ ] **Acceptance:** Stable flush workloads reuse native capacity safely.
