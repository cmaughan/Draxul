# Sweep ligature dirty runs once

**Summary:** Avoid repeatedly checking the same text during redraws, reducing unnecessary work when updating large terminal windows.

**Source:** `libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp`  
**Priority:** P1; static cost model, high confidence. **Reported by:** Claude, Codex. Lines 64–116 expand every dirty cell through neighboring runs, append duplicates, then sort; lines 288–300 call this on a ligature-enabled flush. A homogeneous 300×80 full redraw emits 7.2 million coordinates for 24,000 cells; reused scratch capacity does not remove that work.

- [ ] **Baseline:** Count emitted coordinates, comparisons, scratch capacity, and flush time as row width grows on full redraw and scroll workloads.
- [ ] **Implement:** Sweep or merge touched row intervals and emit each cell once, retaining both adjacent runs when an edit breaks their chain.
- [ ] **Functional safety:** Preserve `////` regrouping, highlights, wide cells, atlas retry/reset, and final cell order.
- [ ] **Compare:** Require emitted work to scale with covered cells, recording before/after flush time without claiming a preset speedup.
- [ ] **Platforms:** Check Vulkan and Metal ligature renders and the scope-appropriate aggregate plus same-cache smoke.
- [ ] **Acceptance:** Identical rendered cells with no quadratic temporary expansion.
