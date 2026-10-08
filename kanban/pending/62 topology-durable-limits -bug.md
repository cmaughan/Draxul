# Keep live session changes within saving limits

**Summary:** Keep accepted session changes within saving limits so successful changes survive a restart.

**Priority:** P1  
**Source:** `libs/draxul-server/src/topology_service.cpp`

**Evidence and trigger:** B10; live names, Space counts, tab counts, and split depth can exceed durable limits, preventing subsequent checkpoints.

- [ ] **Investigate:** Compare every relevant live mutation limit with current-format session validation.
- [ ] **Fix:** Share bounds and validate before publishing mutation side effects.
- [ ] **Acceptance:** Over-limit names and layouts are rejected without changes; accepted boundary values save and restore successfully.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke.
