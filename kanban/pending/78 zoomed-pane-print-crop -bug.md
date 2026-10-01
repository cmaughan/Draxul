# Print the displayed zoomed viewport

**Summary:** Print the full visible zoomed pane so its contents are not cut off.

**Priority:** 84  
**Severity:** MEDIUM  
**Source:** `app/app.cpp`

**Evidence and trigger:** B28; printing crops with the stored split rectangle rather than the separately displayed zoomed viewport.

- [ ] **Investigate:** Compare split geometry, displayed host geometry, chrome offsets, and content hints.
- [ ] **Fix:** Snapshot the displayed viewport and matching print hint for capture.
- [ ] **Acceptance:** Printing zoomed left, right, top, and bottom panes captures their visible content with correct offsets; unzoomed printing remains correct.
- [ ] **Validation:** Run core aggregate tests, relevant capture checks, and same-cache smoke.
