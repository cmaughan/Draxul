# Preserve OSC 8 hyperlinks during terminal restoration

**Severity:** HIGH  
**Type:** Bug

## Bug description

Local scrollback and resize restoration reconstruct cells through `Grid::set_cell()`, which clears `hyperlink_id`, so previously valid OSC 8 links stop working.

**Trigger:** Display an OSC 8 link and then restore cells by scrolling back, resizing, or pulling scrollback rows into a grown viewport.

## Investigation

- [x] Enumerate every `Cell`-to-Grid restoration path in `LocalTerminalHost` and `ScrollbackBuffer`.
- [x] Identify hyperlink and detected-URL IDs as the semantic fields lost by reconstruction.
- [x] Add full-cell and alternate-screen hyperlink preservation coverage; scrollback integration remains covered by the host restoration path.

## Fix strategy

- [x] Add a safe full-cell restoration API or restore hyperlink IDs explicitly after leader cells.
- [x] Preserve wide-cell invariants while restoring semantic metadata.
- [x] Coordinate adjacent-cell behavior with `kanban/pending/28 grid-wide-glyph-overlap-orphan -bug.md`.

## Acceptance criteria

- [ ] OSC 8 links remain resolvable and clickable after scrollback round trips and viewport resize.
- [ ] Wide hyperlinks retain correct leader and continuation metadata.
- [ ] Existing URL detection, selection, and terminal restoration tests pass.
