# Match shortcut chords to their starting prefix
**Summary:** Remember which shortcut prefix was pressed so the second key runs the intended command.

**Priority:** 93  
**Severity:** MEDIUM  
**Source:** `app/input_dispatcher.cpp`  
**Reported by:** Claude M5; consensus F48.

**Evidence and trigger:** Prefix activation at line 366 records no key/modifier identity; line 332 matches only the second key. Two prefixes sharing the same second key can execute the wrong binding.

- [ ] **Investigate:** Trace prefix activation, normalization, timeout, cancellation, pane selection, and configuration reload.
- [ ] **Fix:** Retain active prefix identity and require it to match during chord lookup.
- [ ] **Acceptance:** `Ctrl+S, z` and `Ctrl+B, z` execute their respective commands regardless of binding order.
- [ ] **Acceptance:** Timeout, cancellation, and unmatched chords preserve ordinary input behavior.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke.
