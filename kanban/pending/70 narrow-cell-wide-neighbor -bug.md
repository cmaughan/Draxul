# Preserve nonoverlapping wide characters

**Summary:** Preserve a nearby wide character when updating a narrow character that does not overlap it.

**Priority:** P1  
**Source:** `libs/draxul-grid/src/grid.cpp`

**Evidence and trigger:** B13; writing a narrow character immediately before a wide leader clears that unrelated leader and continuation.

- [ ] **Investigate:** Characterize narrow replacements, wide replacements, and writes over continuation cells.
- [ ] **Fix:** Restrict neighboring-leader cleanup to actual overlap.
- [ ] **Acceptance:** Updating the first cell of `" 界"` to `"A"` preserves the wide glyph; overlapping writes still remove orphaned continuations.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke.
