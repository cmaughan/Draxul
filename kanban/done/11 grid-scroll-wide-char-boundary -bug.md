# 11 Grid Scroll Wide-Char Boundary Repair

## Why This Exists

`Grid::scroll()` runs a "repair pass" after moving rows/columns to fix wide-character pairs that were split by the scroll. The repair pass iterates the full `[top, bot)` row range and replaces orphaned wide-char halves with spaces.

The bug: the repair pass does **not** restrict itself to `[left, right)`. It operates on the entire row width, including columns outside the scroll region. A double-width character whose left half is at column `right - 1` (just inside the boundary) and whose right half is at `right` (just outside) will have its right half "repaired" even though that column was not part of the scroll operation. This can clobber cells in an adjacent region.

The edge case is not currently tested.

**Source:** `libs/draxul-grid/src/grid.cpp` — `Grid::scroll()` repair pass.
**Raised by:** Claude.

## Goal

Restrict the repair pass to only inspect and mutate cells within `[left, right)`. Wide-character pairs that straddle the boundary but whose continuation half falls outside the region should not be touched.

## Implementation Plan

- [x] Read `libs/draxul-grid/src/grid.cpp` and `libs/draxul-grid/include/draxul/grid.h` to confirm the current scroll and repair behavior.
- [x] Identified the repair loop — the second pass that iterates `[top, bot)` looking for split wide chars.
- [x] Narrowed the inner loop to `[left, right)` only:
  - [x] When the wide-char leader is at `right - 1`, clear the leader inside the region and leave `right` untouched.
  - [x] When a continuation lands at `left`, clear the orphaned continuation without mutating `left - 1`.
- [x] Added the paired regression tests from work item 15 alongside this fix.
- [x] Ran `clang-format` on touched files.
- [x] Ran `ctest --test-dir build --build-config Release --output-on-failure`.

## Tests

- [x] Work item [[15 grid-scroll-wide-char-test -test]] was implemented in the same pass and passed under `ctest`.

## Sub-Agent Split

Single agent. Fix is confined to `grid.cpp`. The test work item (15) can be done by the same agent in the same pass.
