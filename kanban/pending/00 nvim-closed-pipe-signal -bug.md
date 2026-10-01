# Handle closed editor pipes safely

**Summary:** Handle a closed editor connection safely so an input operation cannot terminate the application.

**Priority:** 68  
**Severity:** CRITICAL  
**Source:** `libs/draxul-nvim/src/nvim_process.cpp`

**Evidence and trigger:** B03; line 566 writes without parent-side signal suppression. An editor closing its input before a notification can terminate the macOS client.

- [ ] **Investigate:** Trace parent signal handling and child inheritance, including the test runner’s existing suppression.
- [ ] **Fix:** Suppress the signal for parent writes while preserving default child handling; coordinate cancellable writes with B14.
- [ ] **Acceptance:** An isolated client with default initial signal handling survives a closed-pipe write and reports failure.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; verify the macOS production path.
