# Retain useful font and ligature cache entries

**Summary:** Keep useful text-shaping results within a fixed memory limit so scrolling through familiar text does not repeatedly discard and rebuild the whole cache.

**Source:** `libs/draxul-font/src/ligature_analyser.h`  
**Priority:** P2; static, medium-high confidence. **Reported by:** Claude. Lines 184–188 and `font_selector.h:303–308` clear whole caches at their 4,096-entry limit; `grid_rendering_pipeline.cpp:157–202` generates candidate strings on dirty code grids. Repeated working sets above the cap can reshuffle and re-shape the same text.

- [ ] **Baseline:** Count hits, misses, clears, `hb_shape`, and FreeType loads on repeated large code-grid scrolls with varied styles.
- [ ] **Implement:** Choose a bounded retention policy from the observed working set, isolating frequent cell choices from candidate churn.
- [ ] **Functional safety:** Preserve font/size/style invalidation, fallback selection, ligature grouping, and memory bounds.
- [ ] **Compare:** Report steady-state misses and shaping cost; do not assume LRU at the same cap or a preset 64K limit solves sequential replay.
- [ ] **Platforms:** Verify font renders and aggregate/smoke on Windows and macOS.
- [ ] **Acceptance:** Repeated stable text avoids whole-cache churn within an explicit byte budget.
