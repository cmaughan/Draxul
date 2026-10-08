# Decode wide editor cells without shifting the line
**Summary:** Preserve the editor’s cell positions so wide characters do not shift the rest of a line.

**Priority:** P1  
**Source:** `libs/draxul-nvim/src/ui_events.cpp`  
**Reported by:** Claude H3; consensus F24.

**Evidence and trigger:** Lines 294–296 advance two columns for a wide leader before its explicit empty continuation cell arrives. The official line-grid protocol sends that follower.

**Related:** `kanban/done/70 narrow-cell-wide-neighbor -bug.md`.

- [x] **Investigate:** Trace protocol cells, repeats, partial updates, continuation handling, and grid cleanup.
  Confirmed against the Neovim 0.12 `api-ui-events` doc: the right cell of a double-width char is sent as an empty string and wide chars never use `repeat`. The decoder advanced two columns for the leader, so the follower and everything after it landed one column right per wide glyph. The macOS `unicode-view` reference had been blessed with this shifted layout and still passed through its 2.1% changed-pixel tolerance.
- [x] **Fix:** Advance once per protocol cell and handle empty wide followers without erasing their leader.
  `handle_grid_line` now advances one column per cell and skips an empty follower whose left neighbour is a wide leader written earlier in the packet or already in the grid (new optional `IGridSink::sink_is_wide_leader`, implemented by `Grid`). Empty cells with no leader still write an empty cell.
- [x] **Acceptance:** Protocol-shaped wide-character packets preserve following text, highlights, cursor alignment, and end-of-line content.
  `tests/ui_events_tests.cpp` "ui event handler decodes protocol-shaped wide cells without shifting the line".
- [x] **Acceptance:** Updates beginning at a continuation cell and wide-to-narrow replacements retain valid grid state.
  `tests/ui_events_tests.cpp` "ui event handler keeps valid wide state across partial grid_line updates" (also relies on `kanban/done/70 narrow-cell-wide-neighbor -bug.md`).
- [x] **Validation:** Correct fixtures that omit explicit followers; run core aggregate tests, Unicode rendering checks, and same-cache smoke.
  Added followers to the grapheme-width fixture and rewrote the repeat/wide overrun bounds fixture into protocol shape, keeping a malformed repeated-wide packet as a valid-state check. `draxul-render-unicode-view`, `basic-view`, and `cmdline-view` pass; `unicode-view.macos.bmp` re-blessed with the corrected spacing. Aggregate run: only failures that also fail on the unmodified baseline or pass on isolated rerun; smoke passed.
- [ ] **Windows:** Re-bless `tests/render/reference/unicode-view.windows.bmp` (`py do.py blessunicode`) on Windows. It still encodes the shifted wide-glyph layout; it was not regenerated because Windows is unavailable in this session.
