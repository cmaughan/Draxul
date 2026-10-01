# Skip selection overlay rebuilds for unchanged cell endpoints

**Summary:** Keep the existing selection highlight when the pointer stays within the same character so tiny mouse movements do not rebuild an unchanged selection.

**Source:** `libs/draxul-host/src/selection_manager.cpp`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude. Lines 48–56 rebuild during every active drag; lines 262–324 can emit many selected overlay cells. Pixel motion within one terminal cell can repeat the same bounded but large work.

- [ ] **Baseline:** Count overlay builds and cells emitted for a large selection while many pointer events stay within one cell.
- [ ] **Implement:** Return early when endpoint and grid revision are unchanged, with explicit invalidation on content/resize.
- [ ] **Functional safety:** Preserve drag threshold, truncation notice, selection changes, and drag end.
- [ ] **Compare:** Require no repeated overlay build for unchanged endpoints.
- [ ] **Platforms:** Check mouse selection on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** Sub-cell pointer movement does not regenerate identical overlays.
