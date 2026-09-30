# Upload only grid state missing from each frame slot

**Source:** `libs/draxul-renderer/src/renderer_state.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. `copy_to()` at lines 360–368 sends every base cell; lines 62–77 zero a fixed overlay tail. Both Vulkan (`vk_renderer.cpp:203–224`) and Metal (`metal_renderer.mm:106–132`) copy this state on draw. Existing dirty-range methods are unused by the backends.

- [ ] **Baseline:** Count copied, zeroed, flushed, and GPU-uploaded bytes for four panes during a one-cell edit, cursor blink, and unrelated-pane animation.
- [ ] **Implement:** Track content generation and missing ranges per frame slot, with separate cursor/overlay state and full initialization after reallocation.
- [ ] **Functional safety:** Cover two-slot alternation, overlay shrink, resize, aborted frame, buffer reuse, and stale-range retirement.
- [ ] **Compare:** Require zero unchanged base-cell bytes after slot priming; report frame CPU and bytes before/after.
- [ ] **Platforms:** Run Vulkan and Metal snapshots, renderer tests, aggregate and smoke.
- [ ] **Acceptance:** Both slots receive edits exactly once without copying unchanged grids every draw.
