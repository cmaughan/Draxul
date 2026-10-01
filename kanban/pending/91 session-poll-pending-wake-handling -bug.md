# Honor pending wakes in the session poll worker
**Summary:** Remember background wake requests so input and shutdown are not delayed during recovery.

**Priority:** 91  
**Severity:** MEDIUM  
**Source:** `libs/draxul-client/src/remote_session_coordinator.cpp`  
**Reported by:** Claude M3; consensus F46.

**Evidence and trigger:** Lines 2504 and 2559 wait without a predicate despite remembered pending state. A wake arriving before either wait can be lost, including during five-second recovery backoff.

- [ ] **Investigate:** Trace poll success/failure waits, pending state, input wake, stopping, and joining.
- [ ] **Fix:** Use the existing predicate-based worker wait helper and consume pending state consistently.
- [ ] **Acceptance:** A wake published before either wait is honored; stopping interrupts recovery backoff promptly.
- [ ] **Acceptance:** Healthy idle polling retains intended pacing without spinning.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke with controlled pre-wait input/stop interleavings.
