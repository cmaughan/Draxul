# Size toast geometry to the toast

**Source:** `app/toast_host.cpp`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude. Lines 121–149 use a full-window grid for a small toast, and its animation schedules roughly 33 ms redraws. Dirty upload improvements reduce bytes but do not remove full-window instances and background quads.

- [ ] **Baseline:** Count instances, upload bytes, and toast frame time at increasing window sizes for a fixed message.
- [ ] **Implement:** Use toast-bounded grid and viewport geometry.
- [ ] **Functional safety:** Preserve corner anchoring, multiple-toast stacking, fade, clipping, DPI, and glyph atlas behavior.
- [ ] **Compare:** Require fixed-toast draw work to remain bounded as the window grows.
- [ ] **Platforms:** Check Vulkan and Metal toast snapshots, aggregate and smoke.
- [ ] **Acceptance:** A small toast no longer draws a window-sized grid.
