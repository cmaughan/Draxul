# Permit replacement at the pane limit

**Summary:** Allow an agent to replace an existing terminal even when the tab has reached its pane limit.

**Priority:** 87  
**Severity:** MEDIUM  
**Source:** `libs/draxul-server/src/topology_service.cpp`

**Evidence and trigger:** B34; the pane-count guard precedes replacement handling, rejecting an operation that adds no pane.

- [ ] **Investigate:** Trace replacement and additional-pane allocation, including failure rollback.
- [ ] **Fix:** Apply the count limit only to operations that increase pane count.
- [ ] **Acceptance:** Replacement succeeds in a full valid tab, new-pane launch remains rejected, and invalid replacement targets remain rejected.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke.
