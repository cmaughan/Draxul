# Selection Truncation Boundary Test

**Type:** test
**Priority:** 15
**Raised by:** Claude

## Summary

Add unit tests for the selection copy path that exercise the boundary conditions around `kSelectionMaxCells`: selections at the limit, one over the limit, and well over the limit. Tests should verify no out-of-bounds access, correct truncation behaviour, and (after work item `04`) that the raised default allows reasonable selections without truncation.

## Background

Work item `04` raises `kSelectionMaxCells` from 256 to 8192+. This test item provides regression coverage for the selection size limit logic, ensuring that the cap is enforced without out-of-bounds writes and that the resulting clipboard content is a correct prefix of the selected text (not garbled data).

## Implementation Plan

### Files to modify
- `libs/draxul-host/tests/` — add `selection_test.cpp` or extend existing selection tests
- `libs/draxul-host/CMakeLists.txt` — register with ctest

### Steps
- [x] Write test: select exactly `kSelectionMaxCells` cells — full content copied, no truncation
- [x] Write test: select `kSelectionMaxCells + 1` cells — content truncated to limit, no OOB access
- [x] Write test: select `kSelectionMaxCells * 2` cells — content truncated to limit, no OOB access
- [x] Write test: select 0 cells — empty clipboard result, no crash
- [x] Write test: select cells containing multi-byte UTF-8 sequences — verify truncation does not split a UTF-8 sequence mid-character
- [x] Write test: verify that the truncated clipboard content is a valid, complete prefix of the selected text
- [x] Run under ASan to verify no buffer overruns (deferred to work item 20 asan-lsan-ci)
- [x] Register with ctest

## Depends On
- `04 selection-silent-truncation -bug.md` — the default limit should be raised before writing tests against the new value

## Blocks
- None

## Notes
The UTF-8 boundary test is important: if the selection limit is enforced in bytes, it must not truncate at a position that splits a multi-byte sequence. If the limit is in codepoints or cells, this is less of a concern but should still be verified.

> Work item produced by: claude-sonnet-4-6
