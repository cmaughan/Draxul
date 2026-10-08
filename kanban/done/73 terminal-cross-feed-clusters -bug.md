# Preserve character clusters across output chunks

**Summary:** Keep accented and joined characters together when terminal output arrives in separate pieces.

**Priority:** P1  
**Source:** `libs/draxul-terminal-core/src/vt_parser.cpp`

**Evidence and trigger:** B16; final clusters are committed at every feed, so a later combining accent or joined character cannot extend its base.

- [x] **Investigate:** Trace cluster continuation across output chunks, cursor movement, wrapping, and control sequences.
  Confirmed: `VtParser::feed` flushes every cluster at the end of each feed, so a combining mark, variation selector, emoji modifier, or ZWJ-joined codepoint at the start of the next feed became its own cell. Controls and escapes already terminate a cluster in single-feed parsing, so only feed-boundary splits needed continuation state.
- [x] **Fix:** Preserve enough preceding-cluster state to extend continued characters without delaying ordinary visible output.
  The parser still emits every cluster immediately, but keeps the last cluster open (plus its trailing-ZWJ state) until a control or escape. Continuation codepoints in a later feed go to a new optional `on_cluster_continue` callback. `TerminalCore` records where it placed the last cluster and rewrites it in place from that position, so width changes and wrap handling match a single-feed write. If that cell has since changed, it falls back to writing the continuation on its own.
- [x] **Acceptance:** Chunked and unchunked accented and joined text yield matching cells and cursor positions, including wrap boundaries.
  `tests/terminal_vt_tests.cpp` "terminal: clusters split across output chunks match unsplit output" compares every two-chunk split and byte-at-a-time delivery against single-feed output. Cases cover combining, ZWJ, modifier, a VS16 widening at the wrap column, the last column, and control/escape interruptions. Also "terminal: a combining mark in a later chunk extends the previous cell".
- [x] **Validation:** Run core aggregate tests and same-cache smoke.
  `do.py test debug`: only failures that also fail on the unmodified baseline or pass on isolated rerun; `do.py smoke debug --skip-build` passed.
