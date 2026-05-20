# Duplicate utf8_sequence_length Removal

**Type:** refactor
**Priority:** 28
**Raised by:** Claude
**Status:** COMPLETE

## Summary

`libs/draxul-host/src/terminal_host_base.cpp` defines `utf8_sequence_length` locally. The same logic already exists in `libs/draxul-nvim/src/unicode.h`. Remove the local definition and include the shared header, eliminating the duplication and the risk of the two implementations diverging.

## Background

Duplicated utility functions are a maintenance hazard: bugs fixed in one copy do not propagate to the other, and subtle differences between the two implementations can cause inconsistent behaviour depending on which code path is active. The shared `unicode.h` in `draxul-nvim` is the canonical location for UTF-8 utilities; `terminal_host_base.cpp` should consume it rather than reimplementing it.

## Implementation Plan

### Files to modify
- `libs/draxul-host/src/terminal_host_base.cpp` — remove the local `utf8_sequence_length` function definition; add `#include` for the shared header
- `libs/draxul-host/CMakeLists.txt` — if `draxul-nvim` is not already a dependency of `draxul-host`, add it (or move `unicode.h` to `draxul-types` if that is a more appropriate shared location)

### Steps
- [x] Locate the local `utf8_sequence_length` definition in `terminal_host_base.cpp`
- [x] Locate the canonical definition in `libs/draxul-nvim/src/unicode.h`
- [x] Verify the two implementations are semantically identical (same edge cases for 0x00, lead bytes, continuation bytes, overlong sequences)
- [x] If they differ, reconcile in the canonical version first
- [x] Add the appropriate `#include` in `terminal_host_base.cpp`
- [x] If `unicode.h` is in `libs/draxul-nvim/src/` (not a public header), consider promoting it to `libs/draxul-nvim/include/draxul/unicode.h` or moving it to `draxul-types` so both libraries can include it without a circular dependency
- [x] Remove the local definition
- [x] Build and run `ctest` to confirm no regressions

## Completion Notes

After the WI-23 refactor, the local `utf8_sequence_length` was in `libs/draxul-host/src/vt_parser.cpp` (not `terminal_host_base.cpp`). The file already included `<draxul/unicode.h>` from `draxul-types`. The canonical `unicode.h` in `draxul-types` did not yet have `utf8_sequence_length`, so it was added there as an inline function. The local definition in `vt_parser.cpp` was then removed. Build, tests, and smoke test all pass.

## Depends On
- None

## Blocks
- None

## Notes
Check whether `draxul-host` already links against `draxul-nvim`. If not, avoid adding a link dependency just for one header — instead, move `unicode.h` to `draxul-types` (which is already a dependency of both). The dependency graph in `CLAUDE.md` confirms `draxul-types` is a shared header-only library accessible to all.

> Work item produced by: claude-sonnet-4-6
