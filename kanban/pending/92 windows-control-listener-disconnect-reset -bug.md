# Reset failed retained Windows listeners
**Summary:** Reset failed listeners so a quick disconnect cannot leave one retrying forever.

**Priority:** 92  
**Severity:** MEDIUM  
**Source:** `libs/draxul-control/src/control_transport_win32.cpp`  
**Reported by:** Claude M4; consensus F47.

**Evidence and trigger:** Synchronous errors at line 499 fall through without disconnecting the retained first pipe at line 583. Early disconnect can leave repeated `ERROR_NO_DATA`.

**Related:** `kanban/pending/66 windows-stream-accept-failure -bug.md` owns a separate stream-listener failure.

- [ ] **Investigate:** Trace retained/additional listener states, synchronous errors, pending completion, and cancellation.
- [ ] **Fix:** Disconnect/reset failed retained instances and apply useful error reporting and bounded retry pacing.
- [ ] **Acceptance:** Early disconnect does not leave a tight retry loop or permanently consume a listener slot.
- [ ] **Acceptance:** Subsequent clients connect and listener shutdown completes.
- [ ] **Validation:** Run Windows control/server coverage, core aggregate tests, and same-cache smoke.
