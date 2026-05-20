# Selection Silently Truncates at 256 Cells

**Type:** bug
**Priority:** 04
**Raised by:** Claude

## Summary

`kSelectionMaxCells = 256` in `libs/draxul-host/src/terminal_host_base.h` limits the selection buffer to approximately 3–5 lines of text. Selections longer than this are silently truncated when copied to the clipboard with no indication to the user.

## Background

256 cells at a typical 80-column terminal is only 3.2 lines. Modern terminal usage routinely involves copying multi-line command output, log snippets, or code blocks that can easily exceed this. The silent truncation is particularly harmful because the user has no indication that the copied text is incomplete. The immediate fix is to raise the default significantly (8192+ cells) and track this as a prerequisite for the configurable selection limit feature (work item `35`).

## Implementation Plan

### Files to modify
- `libs/draxul-host/src/terminal_host_base.h` — increase `kSelectionMaxCells` from 256 to a more reasonable default (8192 or larger)
- `libs/draxul-host/src/terminal_host_base.cpp` — add a warning log or assertion when the selection is capped so future regressions are visible during development

### Steps
- [x] Increase `kSelectionMaxCells` to at least 8192 (100 lines × 80 columns = 8000 cells minimum; 16384 is a safe round number)
- [x] Search for any allocation or stack-size assumptions that use `kSelectionMaxCells` directly (ensure it drives a heap allocation, not a stack buffer)
- [x] Add a debug log message when the cap is hit so developers can observe it
- [ ] Verify clipboard copy of large selections works end-to-end after the change

## Depends On
- None

## Blocks
- `15 selection-truncation-boundary -test.md`
- `35 configurable-selection-limit -feature.md` — the feature exposes this constant in config; fix the default first

## Notes
The selection limit should ultimately be configurable (work item `35`). This work item focuses only on fixing the dangerously low default so the immediate user-facing bug is resolved quickly, independently of the config work.

> Work item produced by: claude-sonnet-4-6
