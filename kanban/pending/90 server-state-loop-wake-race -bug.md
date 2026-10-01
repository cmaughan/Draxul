# Make server wake notifications reliable
**Summary:** Make wake-up signals reliable so pending work does not wait for the idle timeout.

**Priority:** 90  
**Severity:** MEDIUM  
**Source:** `libs/draxul-server/src/server_kernel_lifecycle.cpp`  
**Reported by:** Claude M2; consensus F45.

**Evidence and trigger:** Notifiers at lines 317, 337, and 716 do not coordinate with the wait mutex at line 643. A notification between predicate evaluation and blocking is lost; terminal output only notifies on its first pending transition.

- [ ] **Investigate:** Trace every control, stream, terminal-output, and stop notifier and the loop predicate.
- [ ] **Fix:** Coordinate flag publication/notification with the waiter mutex while preserving coalescing and teardown safety.
- [ ] **Acceptance:** Controlled publication at the predicate-to-wait boundary wakes the loop without waiting for the idle timeout.
- [ ] **Acceptance:** Stop and terminal-output bursts remain reliable without introducing busy polling.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; retain concurrency coverage.
