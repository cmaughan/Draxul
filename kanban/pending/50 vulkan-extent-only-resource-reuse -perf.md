# Reuse compatible Vulkan resources during extent changes

**Summary:** Reuse graphics resources that are unaffected by window size so resizing on Windows does not repeatedly rebuild the same drawing setup.

**Source:** `libs/draxul-renderer/src/vulkan/vk_renderer.cpp`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude. Lines 756–813 recreate pipelines, descriptor pools/sets, and render-pass-related resources during frame-resource rebuilds on resize. Extent changes alone do not necessarily change their compatibility.

- [ ] **Baseline:** Count pipeline, descriptor, and render-pass creations and drag-frame p95 during a fixed Windows resize sequence.
- [ ] **Implement:** Reuse compatible invariant resources; recreate when format, layout, or device requirements change.
- [ ] **Functional safety:** Preserve transactional failure, swapchain out-of-date, minimize/restore, and dependent Markdown pass compatibility.
- [ ] **Compare:** Require invariant creation counts to remain flat for extent-only steps.
- [ ] **Platforms:** Run Windows Vulkan validation and render checks; inspect shared contracts and macOS smoke.
- [ ] **Acceptance:** Extent-only resize does not repeatedly rebuild compatible pipelines.
