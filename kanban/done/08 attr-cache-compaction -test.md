# Test: Attribute Cache Compaction Safety

**Type:** test
**Priority:** 8
**Source:** Claude review

## Problem

The highlight/attribute table has a compaction mechanism: when the number of live attr IDs exceeds `kAttrCompactionThreshold`, unused IDs are evicted. There is no test that verifies the fundamental correctness invariant: **no attr ID that is actively referenced by a live grid cell must ever be evicted**.

If compaction is wrong it produces silent visual corruption (cells rendered with wrong highlights) that is hard to detect outside a render snapshot test.

**Note:** This test work item is best done alongside `15 attribute-cache-shared-class -refactor`. The refactor centralises the compaction logic, making the invariant trivial to test.

## Investigation steps

- [x] Read `libs/draxul-host/src/terminal_host_base.cpp` — find `attr_id()` and the compaction logic.
- [x] Read `libs/draxul-nvim/src/ui_events.cpp` — find the second copy of the same logic.
- [x] Understand the data structures: what is `kAttrCompactionThreshold`? How is the live-cell scan performed?
- [x] Check whether any existing test covers compaction (search for `compaction` or `kAttrCompaction` in `tests/`).

## Test design

Add to `tests/highlight_table_tests.cpp` (or create it).

- [x] **Below threshold**: insert `kAttrCompactionThreshold - 1` unique attrs; verify all survive.
- [x] **At threshold**: insert `kAttrCompactionThreshold` attrs; verify compaction is triggered.
- [x] **Live-cell protection**: create a grid with cells referencing a specific attr ID, fill the table to trigger compaction, assert the referenced attr ID is still present.
- [x] **Eviction of dead attr**: create an attr ID, reference it in no cell, fill the table, trigger compaction — assert the unreferenced ID is evicted.
- [x] **Re-insert after eviction**: after a dead attr is evicted, re-insert it; assert it gets a valid (possibly recycled) ID and renders correctly.

## Acceptance criteria

- [x] All above tests pass under ASan.
- [x] Tests are part of `draxul-tests`.

## Interdependencies

- **`15 attribute-cache-shared-class -refactor`**: refactor first to centralise the logic, then add tests.
- **`03 ui-events-attr-id-truncation -bug`**: add a test case with a large attr ID (> INT_MAX) to cover bug `03`.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
