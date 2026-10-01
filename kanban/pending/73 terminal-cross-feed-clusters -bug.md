# Preserve character clusters across output chunks

**Summary:** Keep accented and joined characters together when terminal output arrives in separate pieces.

**Priority:** 79  
**Severity:** HIGH  
**Source:** `libs/draxul-terminal-core/src/vt_parser.cpp`

**Evidence and trigger:** B16; final clusters are committed at every feed, so a later combining accent or joined character cannot extend its base.

- [ ] **Investigate:** Trace cluster continuation across output chunks, cursor movement, wrapping, and control sequences.
- [ ] **Fix:** Preserve enough preceding-cluster state to extend continued characters without delaying ordinary visible output.
- [ ] **Acceptance:** Chunked and unchunked accented and joined text yield matching cells and cursor positions, including wrap boundaries.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke.
