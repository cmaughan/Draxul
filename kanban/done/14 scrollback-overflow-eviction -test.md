# Scrollback Overflow Eviction Test

**Type:** test
**Priority:** 14
**Raised by:** Claude

## Summary

Add unit tests for the scrollback buffer that verify FIFO eviction order, correct offset clamping, and absence of memory corruption when the buffer fills beyond `kScrollbackCapacity`. Tests should confirm that the oldest lines are evicted first and that scroll-offset variables are clamped to valid ranges after eviction.

## Background

Scrollback buffers have well-known edge cases around eviction: off-by-one errors in the ring-buffer implementation, stale offset pointers that reference evicted lines, and length-tracking bugs that cause content to disappear or appear duplicated. Given that `kScrollbackCapacity = 2000` (and work item `34` will make this configurable), tests should exercise both small and large capacities to ensure the buffer handles boundary conditions correctly.

## Implementation Plan

### Files to modify
- `tests/scrollback_overflow_tests.cpp` — created

### Steps
- [x] Write test: fill buffer to capacity - 1, verify all lines accessible, no eviction yet
- [x] Write test: fill buffer to exactly capacity, verify all lines accessible
- [x] Write test: add one line beyond capacity, verify oldest line is gone, all others shifted correctly
- [x] Write test: add N lines (N > capacity) at once, verify only the last `capacity` lines remain, in correct order
- [x] Write test: scroll offset at bottom, lines evicted → offset is clamped to valid range (not pointing past buffer start)
- [x] Write test: scroll to top, add new lines → viewport correctly follows or stays at saved position depending on scroll-lock behaviour
- [x] Write test: alternate between small and large writes; verify no memory corruption (run under ASan)
- [x] Register with ctest (added to draxul-tests in tests/CMakeLists.txt)

## Depends On
- None

## Blocks
- None

## Notes
Run these tests under AddressSanitizer (see work item `20`) to catch any off-by-one buffer accesses. The tests should be written against the current `kScrollbackCapacity = 2000` constant; after work item `34` makes it configurable, the tests should pass a smaller capacity (e.g., 10) for efficient boundary testing.

> Work item produced by: claude-sonnet-4-6
