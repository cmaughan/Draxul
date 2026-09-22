# Preserve OSC 8 hyperlinks during terminal restoration

**Severity:** HIGH  
**Type:** Bug

## Bug description

Local scrollback and resize restoration reconstruct cells through `Grid::set_cell()`, which clears `hyperlink_id`, so previously valid OSC 8 links stop working.

**Trigger:** Display an OSC 8 link and then restore cells by scrolling back, resizing, or pulling scrollback rows into a grown viewport.

## Investigation

- [x] Enumerate every `Cell`-to-Grid restoration path in `LocalTerminalHost` and `ScrollbackBuffer`.
- [x] Identify hyperlink and detected-URL IDs as the semantic fields lost by reconstruction.
- [x] Cover full-cell, alternate-screen, scrollback history/live round trips, and viewport resize restoration.

## Fix strategy

- [x] Add a safe full-cell restoration API or restore hyperlink IDs explicitly after leader cells.
- [x] Preserve wide-cell invariants while restoring semantic metadata.
- [x] Coordinate adjacent-cell behavior with `kanban/pending/28 grid-wide-glyph-overlap-orphan -bug.md`.

## Acceptance criteria

- [x] OSC 8 links remain resolvable and clickable after scrollback round trips and viewport resize.
- [x] Wide hyperlinks retain correct leader and continuation metadata.
- [x] Existing URL detection, selection, and terminal restoration tests pass.

## Validation

- macOS Debug: `CCACHE_DIR=/tmp/draxul-ccache cmake --build build --config Debug --target draxul-test-core --parallel 10`
- Focused restoration coverage: `./build/tests/draxul-test-core '[restoration]'` — 3 cases, 30 assertions passed.
- The focused cases exercise explicit OSC 8 click targets in history and the restored live snapshot, wide-cell leader/continuation metadata through shrink/grow, and detected-URL click and selection after resize restoration.
- Final Debug validation passed all 45 unit CTest entries, the same-cache startup smoke, and all five default Metal render comparisons.
