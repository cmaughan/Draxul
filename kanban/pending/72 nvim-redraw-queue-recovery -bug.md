# Recover display state after editor queue overflow

**Summary:** Recover the complete editor display when queued updates overflow so changes are not permanently lost.

**Priority:** P1  
**Source:** `libs/draxul-nvim/src/rpc.cpp`

**Evidence and trigger:** B15; the bounded queue drops incremental notifications without notifying the host or requesting resynchronization.

- [ ] **Investigate:** Trace overflow across redraw transactions, flushes, requests, and host draining.
- [ ] **Fix:** Preserve ordered delivery or introduce explicit invalidation and complete display recovery.
- [ ] **Acceptance:** A burst beyond queue capacity cannot leave a discarded unique update permanently missing; memory remains bounded.
- [ ] **Validation:** Coordinate existing notification work; run core aggregate tests and same-cache smoke.
