# Avoid unchanged relayout and fixed overlay storage

**Source:** `libs/draxul-renderer/src/renderer_state.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. `set_grid_size()` at lines 88–143 relayouts on unchanged dimensions; `relayout()` fills cells and a 16,384-cell overlay. Chrome, palette, and toast call sizing repeatedly; each grid handle also reserves overlay space across CPU and GPU slots.

- [ ] **Baseline:** Count relayouts, CPU fills, RSS, and allocated GPU bytes with multiple panes and chrome/sidebar visible.
- [ ] **Implement:** Skip sizing only when dimensions **and padding** are unchanged; grow overlay storage on demand within its current cap.
- [ ] **Functional safety:** Audit callers that relied on implicit clearing; preserve overlay instance bounds, cursor, and layout after actual size changes.
- [ ] **Compare:** Require zero steady-state relayouts and report memory before/after.
- [ ] **Platforms:** Verify chrome/toast/palette snapshots on Vulkan and Metal, aggregate and smoke.
- [ ] **Acceptance:** Unchanged sizing does no full-grid work; overlay capacity follows use.
