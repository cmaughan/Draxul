# Preserve status requests during layout retries
**Summary:** Complete status requests even when a simultaneous layout request encounters a connection failure.

**Priority:** P2  
**Source:** `libs/draxul-client/src/remote_session_client.cpp`  
**Reported by:** Claude M1; consensus F44.

**Evidence and trigger:** Status is dequeued at line 886, then retry continuation at line 902 skips publication. This occurs for transient/resynchronizing command failures, not ordinary invalid commands.

- [ ] **Investigate:** Trace status ownership, command retry, recovery, stop confirmation, and shutdown.
- [ ] **Fix:** Process status independently or retain it before command retry continuation.
- [ ] **Acceptance:** A status/stop request queued beside a transiently failing command receives exactly one completion or explicit cancellation.
- [ ] **Acceptance:** Command retries and ordinary validation failures retain their existing completion behavior.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke with controlled retry failures.
