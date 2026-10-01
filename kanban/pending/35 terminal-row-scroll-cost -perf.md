# Reduce per-line terminal scroll copying

**Summary:** Move terminal rows more efficiently when new output scrolls the screen so each new line does not require copying nearly every visible character.

**Source:** `libs/draxul-terminal-core/src/terminal_core.cpp`  
**Priority/evidence:** P1; static, high confidence. **Reported by:** Claude. `newline()` at lines 542–559 scrolls for each bottom-row LF; `libs/draxul-grid/src/grid.cpp:120–162,581–620` copies and dirty-marks the region cell by cell. A 200×50 region moves roughly 430 KB per output line before downstream rendering.

- [ ] **Baseline:** Replay a fixed 100,000-line capture through terminal/server paths; count cell copies, dirty calls, lines/s, CPU, and output latency.
- [ ] **Implement:** Provide a row-ring or equivalent bulk scroll/dirty operation that preserves intervening VT operations; do not batch blindly across row reads.
- [ ] **Functional safety:** Cover partial regions, wide pairs, attributes/links, alternate screen, scrollback capture, resize, and semantic deltas.
- [ ] **Compare:** Report work per line and end-to-end throughput before/after without weakening backpressure.
- [ ] **Platforms:** Run terminal/core and server integration coverage on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** A bottom-row LF no longer copies and indirectly marks every retained cell.
