# Preserve build locks when process inspection is denied
**Summary:** Keep build ownership locks when Windows cannot inspect the owner so another build cannot enter the same folder.

**Priority:** P2  
**Source:** `do.py`  
**Reported by:** Claude M17; consensus F53.

**Evidence and trigger:** Lines 54–59 use unconfigured `windll` and read ctypes’ private last-error copy. Access denied can be mistaken for a dead owner, allowing lock removal at line 137.

- [ ] **Investigate:** Trace native error retrieval, handle types, liveness results, and stale-lock reclamation.
- [ ] **Fix:** Configure Windows bindings correctly and treat indeterminate liveness conservatively.
- [ ] **Acceptance:** Access denied for a live owner preserves the lock; a confirmed dead owner remains reclaimable.
- [ ] **Acceptance:** Native handles are closed correctly and normal build ownership behavior remains intact.
- [ ] **Validation:** Run relevant workflow coverage, core aggregate tests, and same-cache smoke; verify Windows error outcomes through controlled bindings.
