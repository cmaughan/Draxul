# Test: ScrollbackBuffer Ring-Wrap at Capacity

**Type:** test
**Priority:** 6
**Source:** Claude review

## Problem

`ScrollbackBuffer` is a fixed-capacity ring buffer (`kCapacity = 2000` rows). The wrap-around logic — evicting the oldest row when the buffer is full — is not directly tested. A regression in the ring-pointer arithmetic could cause rows to be corrupted, double-freed, or the viewport offset to be miscalculated.

## Investigation steps

- [x] Read `libs/draxul-host/src/scrollback_buffer.h` (or `.cpp`) and understand:
  - How rows are inserted.
  - How the write pointer wraps.
  - How the viewport offset is tracked and clamped.
  - How `restore()` / `save()` interact with the ring boundary.
- [x] Check existing scrollback tests in `tests/` for coverage.

## Test design

Add to `tests/scrollback_tests.cpp` (or create it).

- [x] **Exactly-at-capacity insert**: push `kCapacity` rows, then push one more. Verify:
  - The buffer still contains exactly `kCapacity` rows.
  - The oldest row is the second row inserted (the first was evicted).
  - The newest row is the `kCapacity + 1`th row inserted.
- [x] **Double-wrap**: push `2 * kCapacity + 1` rows. Verify the ring behaves consistently.
- [x] **Viewport clamp on eviction**: if viewport is scrolled back to the oldest row and that row is evicted, verify the viewport offset is clamped down, not left dangling.
- [x] **Column mismatch on restore**: save a row, resize the grid to a different column count, call `restore()`; verify no OOB access.
- [x] **Empty buffer read**: call read/iterate on an empty buffer; no crash.

## Acceptance criteria

- [x] All above tests pass under `mac-asan`.
- [x] Tests are part of `draxul-tests` and run via `ctest`.

## Interdependencies

None — standalone.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
