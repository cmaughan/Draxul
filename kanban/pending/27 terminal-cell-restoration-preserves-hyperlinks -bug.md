# Preserve OSC 8 hyperlinks during terminal restoration

**Severity:** HIGH  
**Type:** Bug

## Bug description

Local scrollback and resize restoration reconstruct cells through `Grid::set_cell()`, which clears `hyperlink_id`, so previously valid OSC 8 links stop working.

**Trigger:** Display an OSC 8 link and then restore cells by scrolling back, resizing, or pulling scrollback rows into a grown viewport.

## Investigation

- [ ] Enumerate every `Cell`-to-Grid restoration path in `LocalTerminalHost` and `ScrollbackBuffer`.
- [ ] Identify all semantic fields lost by the current reconstruction API.
- [ ] Add link-click and effective-link-ID tests across scrollback and resize operations.

## Fix strategy

- [ ] Add a safe full-cell restoration API or restore hyperlink IDs explicitly after leader cells.
- [ ] Preserve wide-cell invariants while restoring semantic metadata.
- [ ] Coordinate adjacent-cell behavior with `kanban/pending/28 grid-wide-glyph-overlap-orphan -bug.md`.

## Acceptance criteria

- [ ] OSC 8 links remain resolvable and clickable after scrollback round trips and viewport resize.
- [ ] Wide hyperlinks retain correct leader and continuation metadata.
- [ ] Existing URL detection, selection, and terminal restoration tests pass.
