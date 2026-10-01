# Index Kanban metadata filenames during ordering

**Summary:** Look up saved card positions directly so restoring a large Kanban lane does not repeatedly search every card.

**Source:** `modules/kanban/draxul-kanban/src/kanban_store.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Codex. Lines 228–252 search the complete card vector for each saved filename, approaching N² comparisons for a fully listed lane. The separate GUI-worker change moves this CPU work but does not remove it.

- [ ] **Baseline:** Count comparisons and reload CPU for 1,000, 5,000, and 10,000 cards in varied orders.
- [ ] **Implement:** Build a temporary owning filename-to-index map before moving cards.
- [ ] **Functional safety:** Preserve stale and duplicate metadata names and original order of unmatched cards; avoid keys into moved strings.
- [ ] **Compare:** Require lookup growth proportional to card count and identical final order.
- [ ] **Platforms:** Check ordering on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** Metadata ordering no longer repeatedly scans the lane.
