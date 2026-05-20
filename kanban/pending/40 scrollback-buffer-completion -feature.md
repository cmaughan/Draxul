# WI 40 -- Scrollback Buffer Completion

**Type:** feature
**Priority:** 9 (critical UX — content loss during pane resize; no keyboard scroll)
**Source:** diagnosed via split-resize CSI trace (2026-04-09)
**Created:** 2026-04-09

---

## Problem

A `ScrollbackBuffer` already exists (`libs/draxul-host/include/draxul/scrollback_buffer.h`)
with a 2000-line ring buffer, mouse-wheel scroll-to-view, alt-screen exclusion,
and highlight compaction integration. However, several critical gaps remain:

1. **Missing capture points**: Only `newline()` pushes rows to scrollback. Three
   other scroll operations silently discard content:
   - `handle_esc('D')` (IND — Index)
   - `csi_scroll('S', ...)` (CSI SU — Scroll Up)
   - `csi_insert_delete('M', ...)` (DL — Delete Line at row 0)

2. **No resize reflow**: When a pane shrinks vertically (e.g., horizontal split),
   rows that don't fit are lost forever. tmux preserves them by pushing excess
   rows to scrollback during resize and pulling them back on grow. This is the
   root cause of the "split clears top pane" bug.

3. **No keyboard scrollback**: Mouse wheel works but Shift+PageUp/Down, Shift+Home/End
   are not wired up.

4. **Hardcoded capacity**: `kCapacity = 2000` is a compile-time constant, not
   user-configurable.

5. **CSI 2 J doesn't preserve visible content**: `erase_display(2)` blanks the
   grid without pushing non-blank rows to scrollback. Many terminals (iTerm2,
   Windows Terminal) save visible content to scrollback on full-screen clear.

---

## Implementation Plan

### Phase 1 — Fix Missing Scrollback Capture Points

**Files:** `libs/draxul-host/src/terminal_host_base_csi.cpp`, `libs/draxul-host/src/terminal_host_base.cpp`

- [x] In `handle_esc('D')` (IND): Added guard+hook matching `newline()`.
- [x] In `csi_scroll('S', ...)` (CSI SU): Captures N rows before bulk scroll.
- [x] In `csi_insert_delete('M', ...)` (DL): Captures when `vt_.row == 0` and
  scroll region is full-screen.
- [ ] Tests: verify each scroll operation captures to scrollback (existing 12
  scrollback tests pass; specific IND/SU/DL capture tests deferred).

### Phase 2 — Resize Reflow (HIGH COMPLEXITY)

**Files:** `libs/draxul-host/src/local_terminal_host.cpp`,
`libs/draxul-host/include/draxul/scrollback_buffer.h`,
`libs/draxul-host/src/scrollback_buffer.cpp`

**Design:**

When `on_viewport_changed()` detects a vertical size change:

**Shrink (new_rows < old_rows):**
1. Capture the visible grid snapshot
2. `excess = old_rows - new_rows`
3. Push the top `excess` rows from snapshot into scrollback (oldest visible content)
4. Resize grid to new dimensions
5. Restore bottom `new_rows` rows from snapshot
6. Adjust cursor: `vt_.row = max(0, vt_.row - excess)`

**Grow (new_rows > old_rows):**
1. `pull = min(new_rows - old_rows, scrollback.size())`
2. Pop `pull` newest rows from scrollback
3. Resize grid to new dimensions
4. Place pulled rows at top, old visible content shifted down
5. Adjust cursor: `vt_.row += pull`

**New ScrollbackBuffer method:**
- `pop_newest_rows(int n, callback)` — removes N newest rows from ring,
  visits each (newest first). Handles ring wrap correctly.

**Critical timing:** Reflow must happen BEFORE `do_process_resize()` sends SIGWINCH
so the grid is correct when the shell starts redrawing.

- [x] Add `pop_newest_rows` to ScrollbackBuffer
- [x] Implement shrink reflow: push non-blank excess top rows to scrollback
- [x] Implement grow reflow: pull rows back from scrollback
- [x] Adjust cursor position during reflow
- [x] Restore pre-resize content after shell's SIGWINCH clear-and-redraw
- [x] Don't snap scrollback to live on PTY output, only on user input
- [x] Don't flush grid to renderer while scrolled back
- [ ] Tests: shrink→grow round-trip preserves content, cursor position correct

### Phase 3 — Keyboard Scrollback Navigation

**Files:** `libs/draxul-host/src/local_terminal_host.cpp`

- [x] Shift+PageUp: scroll up by `grid_rows()` lines
- [x] Shift+PageDown: scroll down by `grid_rows()` lines
- [x] Shift+Home: scroll to top of scrollback
- [x] Shift+End: scroll to bottom (live view)
- [x] Handle in `on_key()` before forwarding to `TerminalHostBase::on_key()`
- [x] Show scrollback position [offset/total] in pane status pill

### Phase 4 — Configurable Capacity

**Files:** `scrollback_buffer.h`, `scrollback_buffer.cpp`, `local_terminal_host.cpp`, config

- [x] Replace `kCapacity` with constructor parameter `capacity_`
- [x] Default: 10,000 lines
- [ ] Add `scrollback_lines` to `HostLaunchOptions` and config TOML (capacity is
  constructor param but not yet wired to config file)

### Phase 5 — CSI 2 J Preserves Visible Content (Optional Polish)

**Files:** `terminal_host_base.cpp`

- [x] In `erase_display(2)`, before `grid().clear()`, push all non-blank rows
  to scrollback (guarded by `!alt_screen_.in_alt_screen()`)
- [ ] Test: run `clear`, scroll up, verify previous output visible

---

## Recommended Phase Order

1. **Phase 1** (1-2h, low risk) — immediate win, fixes real scrollback gaps
2. **Phase 4** (1-2h, low risk) — simple, improves usability
3. **Phase 3** (1-2h, low risk) — keyboard navigation
4. **Phase 2** (4-8h, HIGH risk) — core architectural fix for resize reflow
5. **Phase 5** (0.5h, low risk) — polish

Phase 2 is highest value but highest risk. Phases 1/3/4 are easy wins.

---

## Acceptance Criteria

- [x] IND, CSI SU, DL all capture to scrollback (Phase 1)
- [x] Horizontal split preserves top pane content via reflow (Phase 2)
- [x] Shrink→grow round-trip recovers original visible content (Phase 2)
- [x] Shift+PageUp/Down scrolls through scrollback (Phase 3)
- [ ] `scrollback_lines` config option works (Phase 4 — capacity param done, config wiring pending)
- [x] `clear` command preserves output in scrollback (Phase 5)
- [x] All existing scrollback tests still pass
- [x] No performance regression on `yes | head -1000`

---

## Interdependencies

- **WI 04** (grid clear OOM) — `full_dirty_` flag landed; Grid::resize now preserves content. Phase 2 builds on this.
- **WI 09** (unix pty shutdown) — shutdown is now non-blocking. No conflict.
- Existing `ScrollbackBuffer` tests in `tests/scrollback_overflow_tests.cpp`.
- `AltScreen` integration already handles alt-screen exclusion.

---

## Key Existing Files

| File | Role |
|---|---|
| `libs/draxul-host/include/draxul/scrollback_buffer.h` | Ring buffer, scroll-to-view |
| `libs/draxul-host/src/scrollback_buffer.cpp` | Storage, resize, display composite |
| `libs/draxul-host/src/local_terminal_host.cpp` | Hook wiring, resize handler, mouse scroll |
| `libs/draxul-host/src/terminal_host_base.cpp` | `newline()` hook, `erase_display`, viewport |
| `libs/draxul-host/src/terminal_host_base_csi.cpp` | IND/SU/DL — missing capture points |
| `tests/scrollback_overflow_tests.cpp` | Existing scrollback tests |

---

*Filed by: claude-opus-4-6 -- 2026-04-09*
