# Allocate scrollback rows as they become live

**Source:** `libs/draxul-terminal-core/src/scrollback_buffer.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude; Codex treated impact as an open lead. `resize()` at lines 49–116 constructs `capacity × columns` cells even when no row is live. Default capacity is 10,000 rows; server admission already reserves against capacity, so this is memory/startup work rather than a reason to relax its budget.

- [ ] **Baseline:** Measure allocated cells, server RSS, pane-create time, and resize peak for one and multiple idle panes within the existing reservation limit.
- [ ] **Implement:** Allocate bounded row blocks on demand while retaining capacity accounting and stable row access semantics.
- [ ] **Functional safety:** Preserve oldest-first iteration, overflow, live-row resize copying, snapshots, and strong failure behavior.
- [ ] **Compare:** Report idle RSS and create/resize latency before/after; avoid configurations rejected by the existing admission budget.
- [ ] **Platforms:** Run terminal/server scrollback coverage on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** An empty pane does not construct all reserved scrollback cells.
