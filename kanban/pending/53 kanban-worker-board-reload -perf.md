# Move full Kanban board reload off the GUI thread

**Source:** `modules/kanban/draxul-kanban/src/kanban_host.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. Lines 265–281 and 466–484 reload after a debounced invalidation; `kanban_store.cpp:255–262,655–660,714–727` opens and scans every card across discovered boards. Debounce limits repeats but leaves a GUI-thread N-file operation.

- [ ] **Baseline:** Inject delayed reads in a 5,000-card workspace; count GUI-thread file opens and typing latency in another pane.
- [ ] **Implement:** Run one owned, coalescing scan with generation-checked publication; consider a bounded unchanged-priority cache.
- [ ] **Functional safety:** Preserve moves, focus/preview, failures, native watcher invalidations, and shutdown.
- [ ] **Compare:** Require no GUI-thread board file scan and report reload latency before/after.
- [ ] **Platforms:** Check Windows and macOS watchers, Kanban aggregate and same-cache smoke.
- [ ] **Acceptance:** Large-board refresh does not block unrelated GUI input.
