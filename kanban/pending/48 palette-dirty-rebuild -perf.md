# Rebuild the command palette only when its view changes

**Summary:** Rebuild the command palette only when its contents or appearance change so unrelated terminal activity does not redraw the same list.

**Source:** `app/command_palette_host.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. Lines 77–100 copy view state, render the palette grid, and update cells on every pump while open. Filtering is already cached, but unrelated input/output still rebuilds unchanged presentation.

- [ ] **Baseline:** Count palette cell builds and pump time while open and idle, then during output in another pane.
- [ ] **Implement:** Track dirty revisions for query, selection, viewport, font/atlas, background, and open/close.
- [ ] **Functional safety:** Preserve key/text input, selection movement, clipping, and atlas reset.
- [ ] **Compare:** Require zero unchanged cell builds after the palette settles.
- [ ] **Platforms:** Check palette snapshots on Vulkan and Metal, aggregate and smoke.
- [ ] **Acceptance:** Unrelated app pumps do not rebuild the same palette cells.
