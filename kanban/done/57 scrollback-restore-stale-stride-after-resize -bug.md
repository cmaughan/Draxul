# Bug: restore_live_snapshot uses stale column stride after terminal resize

**Severity**: HIGH
**File**: `libs/draxul-host/src/scrollback_buffer.cpp:110`
**Source**: review-bugs-consensus.md (H6)

## Description

`restore_live_snapshot` computes cell indices as:
```cpp
const size_t idx = (size_t)r * live_snapshot_cols_ + col;
```
`live_snapshot_cols_` is the column count at the time the snapshot was taken. `cols` (current) is fetched fresh on line 105. If the terminal was resized between `save_live_snapshot` and `restore_live_snapshot`, these values differ.

When `cols > live_snapshot_cols_`: for columns beyond `live_snapshot_cols_`, the index falls into the data of the *next* snapshot row, displaying wrong cell content instead of blanks. The bounds check at line 111 prevents a buffer overrun but does not prevent the visual corruption.

## Trigger Scenario

1. Enter scrollback view (saves live snapshot).
2. Resize the terminal pane while in scrollback.
3. Exit scrollback (restores live snapshot).
4. Columns beyond the original width show data from the wrong rows.

## Fix Strategy

- [x] Change `restore_live_snapshot` to iterate only up to `std::min(cols, live_snapshot_cols_)` columns from the snapshot; blank-fill any extra columns beyond that
- [x] Alternatively, store the snapshot dimensions with the snapshot data (`live_snapshot_cols_` already exists) and ensure the comment makes the invariant explicit
- [x] Add a unit test with resize-before-restore to confirm no cross-row data appears
- [x] Fix H5 (compact_attr_ids missing scrollback) in the same session — see work item 56

## Acceptance Criteria

- [x] Restoring a snapshot after a column increase shows blank cells for the new columns, not data from adjacent rows
- [x] Restoring a snapshot after a column decrease shows the visible portion correctly
- [x] No out-of-bounds access under ASan/UBSan during restore
