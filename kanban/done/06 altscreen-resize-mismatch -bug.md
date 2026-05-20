# Alt-Screen Resize Mismatch

**Type:** bug
**Priority:** 06
**Raised by:** Claude

## Summary

When the terminal is resized while in alt-screen mode (`\e[?1049h` active — as used by vim, less, htop, and similar), the main-screen snapshot stored before entering alt-screen is not re-dimensioned to match the new terminal size. On exit from alt-screen (`\e[?1049l`), the restored content is misaligned with the new terminal dimensions, producing corrupted or garbled output.

## Background

The alternate screen buffer mechanism saves the entire current screen state when entering alt-screen and restores it on exit. If the terminal is resized between entry and exit, the saved snapshot dimensions no longer match the live terminal dimensions. The restore operation then either clips content (if the terminal grew) or writes beyond the new bounds (if the terminal shrank), leaving the terminal in a corrupted visual state. This affects any workflow where the user resizes the window while vim, less, man, or htop is running.

## Implementation Plan

### Files to modify
- `libs/draxul-host/src/terminal_host_base.cpp` — in the resize handler, if currently in alt-screen mode, also re-dimension the main-screen snapshot to the new size before the alt-screen takes it; alternatively, re-dimension the snapshot when restoring it on alt-screen exit
- `libs/draxul-host/src/terminal_host_base.h` — ensure the main-screen snapshot data structure supports resize operations

### Steps
- [x] Identify where the main-screen snapshot is stored (likely a `std::vector<Cell>` or similar with recorded row/col dimensions)
- [x] Identify the resize handler path (`on_resize` or equivalent in `TerminalHostBase`)
- [x] In the resize handler: if `in_alt_screen_` is true, re-dimension the main-screen snapshot to the new `cols × rows`, padding new cells with blank defaults and truncating excess rows/columns
- [x] In the alt-screen exit path: added debug log verifying snapshot size vs current grid dims before restore
- [ ] Test: open vim, resize the window, exit vim — the shell prompt should appear correctly positioned without garbled content

## Depends On
- None

## Blocks
- `13 resize-cascade-integration -test.md` — the resize cascade test should cover this path

## Notes
The main-screen snapshot re-dimensioning logic is essentially the same as the normal grid resize logic (pad with blanks, truncate). Factor out a shared helper if the grid resize and snapshot resize logic would otherwise be duplicated.

> Work item produced by: claude-sonnet-4-6
