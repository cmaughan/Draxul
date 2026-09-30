# Keep grid resize separate from atlas invalidation

**Source:** `libs/draxul-host/src/grid_host_base.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. `apply_grid_size()` at lines 196–219 forces a full shared atlas upload; `grid_rendering_pipeline.cpp:328–343` stages the full 2048² RGBA atlas even if no glyph pixels changed. Atlas dirty-region and reset-generation handling already exist.

- [ ] **Baseline:** Count queued, staged, and GPU atlas bytes during a warmed-atlas multi-pane resize.
- [ ] **Implement:** Invalidate grid geometry without forcing unchanged texture content; retain first-use, font/reset, and device-recreation uploads.
- [ ] **Functional safety:** Check glyph generation, new glyphs during resize, font-size changes, and completed atlas-reset regression.
- [ ] **Compare:** Require zero unchanged texture bytes on geometry-only resize.
- [ ] **Platforms:** Verify text after resize on Vulkan and Metal, aggregate and smoke.
- [ ] **Acceptance:** Grid size changes do not reupload the whole shared atlas unnecessarily.
