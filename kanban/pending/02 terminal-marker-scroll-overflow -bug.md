# Clamp scrolling before moving prompt markers

**Summary:** Limit scrolling before moving saved prompt markers so extreme output cannot cause invalid arithmetic.

**Priority:** 69  
**Severity:** CRITICAL  
**Source:** `libs/draxul-terminal-core/src/terminal_core.cpp`

**Evidence and trigger:** B04; line 533 moves markers before clamping. A row-one prompt marker followed by maximum downward scrolling overflows signed arithmetic.

- [ ] **Investigate:** Trace upward and downward displacement through parsing, markers, and grid scrolling.
- [ ] **Fix:** Clamp once to the valid scrolling region before moving either markers or cells.
- [ ] **Acceptance:** Extreme scroll requests produce defined arithmetic and correct marker retention or removal.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke.
