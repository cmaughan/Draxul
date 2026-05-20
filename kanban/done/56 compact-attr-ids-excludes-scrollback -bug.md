# Bug: compact_attr_ids excludes scrollback buffer cells → wrong colors in scrollback

**Severity**: HIGH
**File**: `libs/draxul-host/src/terminal_host_base.cpp:264–292`
**Source**: review-bugs-consensus.md (H5)

## Description

`compact_attr_ids()` builds the active-attrs set by scanning:
1. Main grid cells (lines 264–273)
2. Alt-screen saved cells (line 275, `alt_screen_.for_each_saved_cell(...)`)

Scrollback buffer rows are never scanned. Any `hl_attr_id` that appears only in the scrollback buffer is absent from the `active_attrs` map. When `HighlightRemapper` encounters an unmapped id, it returns 0 (default highlight). This causes all syntax-colored content in scrollback to render with no colors after compaction. Additionally, scrollback cells are not remapped — their IDs now point to wrong (re-used) highlight slots.

## Trigger Scenario

Use a terminal for long enough to exceed `kAttrCompactionThreshold` unique highlight IDs. After compaction, scrolling back through history shows uncolored (all-default) text.

## Fix Strategy

- [x] Add a `for_each_cell` (or equivalent) method to `ScrollbackBuffer` that iterates all stored cells
- [x] In `compact_attr_ids`, after the alt-screen scan, scan scrollback cells and add their `hl_attr_id` values to `active_attrs`
- [x] After building the remap table, call `scrollback_buffer_.remap_highlight_ids(HighlightRemapper{remap})` similarly to `grid().remap_highlight_ids(...)`
- [x] Fix H6 (stale stride on restore) in the same scrollback session — see work item 57

## Acceptance Criteria

- [x] After attr compaction, scrollback content retains correct syntax colors
- [x] Unit test: populate scrollback with cells using unique highlight IDs, trigger compaction, verify IDs map to correct highlight attributes
- [x] No regression in main grid or alt-screen highlight display
