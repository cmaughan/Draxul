# Avoid waiting for nonexistent connection operations

**Summary:** Finish failed Windows connection attempts promptly so they cannot stop new connections or block shutdown.

**Priority:** P1  
**Source:** `libs/draxul-control/src/async_frame_stream_win32.cpp`

**Evidence and trigger:** B11; synchronous connection failure reaches an infinite wait on an unsignaled event without pending work.

- [ ] **Investigate:** Distinguish synchronous success, synchronous failure, pending completion, and cancellation paths.
- [ ] **Fix:** Cancel and drain only genuinely pending connection operations.
- [ ] **Acceptance:** Immediate disconnect and injected synchronous errors return promptly; listener shutdown completes.
- [ ] **Validation:** Run Windows control/server coverage, core aggregate tests, and same-cache smoke.
