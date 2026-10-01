# Report unavailable temporary storage during printing

**Summary:** Show a printing error when temporary storage is unavailable so a failed print cannot close the application.

**Priority:** 71  
**Severity:** CRITICAL  
**Source:** `app/app.cpp`

**Evidence and trigger:** B09; line 2064 performs throwing temporary-directory lookup outside error handling after successful capture.

- [ ] **Investigate:** Trace temporary-path creation through capture completion and print notifications.
- [ ] **Fix:** Use error-code lookup and report failure before constructing the output document.
- [ ] **Acceptance:** Missing or inaccessible temporary storage leaves the client running and produces a useful print error.
- [ ] **Validation:** Verify normal printing remains functional; run core aggregate tests and same-cache smoke.
