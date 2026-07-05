# Unicode-safe native overlay text

**Type:** bug
**Priority:** 08
**Raised by:** Claude; supported by GPT/Codex's Unicode-risk assessment

## Problem

`palette_renderer.cpp`, `toast_renderer.cpp`, and `tooltip.cpp` emit one `CellText`/glyph lookup per UTF-8 byte. Non-ASCII text becomes invalid clusters, width calculations count bytes, and tooltip rasterization can show tofu or misalign text.

## Implementation plan

- [ ] Add one shared UTF-8-to-cell/cluster iterator in `draxul-gui`, reusing `draxul/unicode.h` and existing width/combining rules.
- [ ] Make the helper produce valid clusters, display width, and continuation-cell information without truncating in the middle of a sequence.
- [ ] Migrate palette, toast, and grid-backed overlay writers to the helper.
- [ ] Migrate tooltip rasterization to the same cluster iteration and the shared text-atlas builder where appropriate; coordinate with the active atlas work.
- [ ] Consolidate the duplicated full-block occlusion helper.
- [ ] Replace `grid.cpp`'s private UTF-8 validation decoder with a shared validated-prefix helper in `draxul/unicode.h`, preserving CellText truncation semantics.
- [ ] Consolidate the duplicated Kanban grapheme/cluster walkers in `kanban_layout.cpp` and `kanban_host.cpp` on the same tested primitive where their rules agree.
- [ ] Define clipping behavior for wide glyphs at the right edge and combining marks at the start of a clipped span.

## Tests

- [ ] Cover accented Latin, CJK, emoji, ZWJ sequences, combining marks, invalid UTF-8, and edge clipping in palette/toast/tooltip paths.
- [ ] Assert cell widths/continuation cells, not only byte strings.
- [ ] Add or update a render scenario only after deterministic unit coverage passes.

## Acceptance criteria

- [ ] Overlay renderers never construct a cluster from an individual UTF-8 continuation byte.
- [ ] Non-ASCII overlay text is correctly aligned and clipped.
- [ ] GUI/font tests, render snapshots, `ctest`, and smoke pass on both backend paths.

## Dependencies and parallelism

Wait for the active text-atlas patch to settle. Blocks clean Chrome/GUI refactors (items 23 and 29).

<model>GPT-5 Codex</model>
