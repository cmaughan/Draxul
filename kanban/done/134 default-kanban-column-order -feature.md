# Default Kanban column order

**Type:** feature
**Priority:** P1 / sequence 134
**Raised by:** user, 2026-09-24

## Goal

Always present the standard Kanban lanes first as Ice Box, Pending, and Done,
in that order. Boards may define additional lanes, which follow the standard
lanes without losing their configured relative order.

## Implementation

- [x] Define one canonical rank for `ice-box`, `pending`, and `done`.
- [x] Apply the default order after directory discovery and saved metadata.
- [x] Apply the default order to aggregate multi-repository boards.
- [x] Preserve the relative order of additional custom lanes.
- [x] Add focused board/store tests for discovery, metadata, and aggregation.

## Validation

- [x] Run the scope-appropriate Debug aggregate tests.
- [x] Run a same-cache Debug startup smoke.

## Notes

- Saved card order remains authoritative within each lane.
- Custom lanes remain supported and appear after the three standard lanes.
- The ordering is applied after metadata restoration and after aggregate workspace
  merging, so saved metadata cannot place a standard lane behind a custom lane.
- Focused Kanban board/store coverage passed: 161 assertions in 18 test cases.
- `python3 do.py test debug` passed all 25 aggregate entries on 2026-09-24.
- `python3 do.py smoke debug --skip-build` passed immediately afterward using the
  same Debug cache; Metal initialized successfully on Apple M5.
