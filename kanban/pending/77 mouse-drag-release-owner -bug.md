# Deliver releases to the active drag owner

**Summary:** Stop dragging when the mouse button is released anywhere so selections do not keep moving afterward.

**Priority:** 83  
**Severity:** MEDIUM  
**Source:** `app/input_dispatcher.cpp`

**Evidence and trigger:** B27; chrome, pills, panel capture, and cross-pane routing can prevent terminal or divider drag cleanup.

- [ ] **Investigate:** Trace press ownership and release filtering for terminal selections and split dividers.
- [ ] **Fix:** Deliver or cancel matching releases before filtering, and clear ownership on focus or lifecycle cancellation.
- [ ] **Acceptance:** Releasing over chrome, another pane, or an overlay stops selection and divider dragging; ordinary clicks still route correctly.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke.
