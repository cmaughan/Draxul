# Validate restored identifiers and counters

**Summary:** Reject saved numbers that cannot support the next item safely so restoring a session cannot overflow its counters.

**Priority:** 70  
**Severity:** CRITICAL  
**Source:** `libs/draxul-session-model/src/session_state.cpp`

**Evidence and trigger:** B05; maximum signed identifiers pass validation, then overflow during server capture or standalone Space and tab restoration.

- [ ] **Investigate:** Inventory identifier and next-counter validation and increment sites across both restoration paths.
- [ ] **Fix:** Reject unsupported ranges and use checked increments with explicit exhaustion handling.
- [ ] **Acceptance:** Boundary identifiers and allocator values cannot overflow during restore, capture, or subsequent creation.
- [ ] **Validation:** Retain valid-session round trips; run core aggregate tests and same-cache smoke.
