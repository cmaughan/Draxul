# Avoid a second live snapshot copy on the GUI thread

**Source:** `libs/draxul-host/src/remote_terminal_host.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. Lines 1204–1208 copy the complete string-bearing snapshot for ordinary live publications, although sparse cells are applied at 1296–1299. Worker publication already owns a snapshot; scrollback composition is the branch that needs a separate value.

- [ ] **Baseline:** Count GUI-thread cell copies, allocations, and pump time for sparse and metadata-only updates at 200×60 and a larger grid.
- [ ] **Implement:** Borrow the live publication within its consumption scope and allocate an owned composed view only for scrollback.
- [ ] **Functional safety:** Preserve snapshot lifetime, resize, highlight exhaustion, selection, and scrollback behavior.
- [ ] **Compare:** Require zero live-branch full copies and record before/after pump latency.
- [ ] **Platforms:** Check remote-terminal host tests and visible output on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** Sparse live updates do not copy the complete grid on the GUI thread.
