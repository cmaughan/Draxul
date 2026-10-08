# Decode wide editor cells without shifting the line
**Summary:** Preserve the editor’s cell positions so wide characters do not shift the rest of a line.

**Priority:** P1  
**Source:** `libs/draxul-nvim/src/ui_events.cpp`  
**Reported by:** Claude H3; consensus F24.

**Evidence and trigger:** Lines 294–296 advance two columns for a wide leader before its explicit empty continuation cell arrives. The official line-grid protocol sends that follower.

**Related:** `kanban/pending/70 narrow-cell-wide-neighbor -bug.md`.

- [ ] **Investigate:** Trace protocol cells, repeats, partial updates, continuation handling, and grid cleanup.
- [ ] **Fix:** Advance once per protocol cell and handle empty wide followers without erasing their leader.
- [ ] **Acceptance:** Protocol-shaped wide-character packets preserve following text, highlights, cursor alignment, and end-of-line content.
- [ ] **Acceptance:** Updates beginning at a continuation cell and wide-to-narrow replacements retain valid grid state.
- [ ] **Validation:** Correct fixtures that omit explicit followers; run core aggregate tests, Unicode rendering checks, and same-cache smoke.
