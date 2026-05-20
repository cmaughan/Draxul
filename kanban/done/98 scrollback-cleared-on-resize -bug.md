# WI 98 — scrollback-cleared-on-resize

**Type:** bug  
**Priority:** 2 (scrollback history lost on every window resize)  
**Source:** review-bugs-consensus.md §M5 [Gemini]  
**Produced by:** claude-sonnet-4-6

---

## Problem

`ScrollbackBuffer::resize()` (`libs/draxul-host/src/scrollback_buffer.cpp:15–28`) calls `storage_.assign(kCapacity * cols, Cell{})` and resets `write_head_`, `count_`, and `offset_` to zero. This destroys the entire scrollback history every time the user resizes the window horizontally. Users lose all terminal history whenever the window is resized, which is unexpected behavior for a terminal emulator.

---

## Investigation

- [ ] Read `libs/draxul-host/src/scrollback_buffer.cpp` fully — understand the ring-buffer structure (`write_head_`, `count_`, `kCapacity`, `storage_` stride).
- [ ] Determine the storage layout: is each row `cols_` cells wide? How are rows indexed?
- [ ] Consider the tradeoffs: a full reflow is ideal but complex; a "best effort" copy (truncate wider rows, blank-fill narrower rows) may be acceptable.

---

## Fix Strategy

- [ ] Before reassigning `storage_`, copy existing rows into a temporary buffer:
  1. Snapshot the current `count_` rows (using the existing `row(i)` accessor).
  2. Allocate the new storage.
  3. Copy rows back, clamping each row to `std::min(old_cols, new_cols)` columns and blank-filling any extra columns.
- [ ] Preserve `write_head_` and `count_` appropriately after the copy.
- [ ] Preserve `live_snapshot_` across the resize if it was active.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] Resizing the window horizontally preserves the scrollback history (rows may be truncated/extended but not dropped).
- [ ] Scrollback offset is reset to 0 after resize (since row layout has changed).
- [ ] Smoke test passes.
