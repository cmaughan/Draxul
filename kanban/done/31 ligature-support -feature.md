# 31 Ligature Support

## Why This Exists

HarfBuzz already shapes text and produces multi-glyph substitutions (ligatures). The current
rendering model assigns exactly one cell per character, so a ligature that spans two characters
(e.g., `fi`, `->`, `=>`) would need a two-cell-wide glyph slot. The `GpuCell` layout has pad
bits available (see `style_flags`) but there is no concept of a "wide glyph cluster" spanning
adjacent cells.

Ligatures are a popular feature for programming fonts (Fira Code, JetBrains Mono) and their
absence is visible to users who expect `->` to render as a single arrow glyph.

**Source:** `libs/draxul-font/src/text_shaper.cpp` (HarfBuzz shaping), `libs/draxul-renderer/src/renderer_state.cpp` (cell layout).
**Raised by:** Claude (identifies the gap clearly), GPT (mentions ligature toggle in config).

## Goal

Implement ligature rendering for programming font ligatures:
1. HarfBuzz shaping produces a glyph that is wider than one cell — render it spanning two cells.
2. The first cell carries the glyph UV and dimensions; the second cell is marked as a "ligature
   continuation" and renders no glyph of its own.
3. A config option `enable_ligatures = true/false` controls whether HarfBuzz ligature substitution
   is applied (callers can disable it for users who prefer raw glyphs).

## Implementation Plan

- [x] Read `libs/draxul-font/src/text_shaper.cpp` to understand how HarfBuzz glyph positions/advances are currently mapped to cells.
- [x] Read `libs/draxul-types/include/draxul/types.h` to understand `GpuCell` layout and available flag bits.
- [x] Design the wide-cluster representation around existing atlas-width quads:
  - Leader cell: stores the combined cluster UVs and full ligature pixel width in the existing `AtlasRegion`.
  - Continuation cell: receives a normal background/style update with an empty glyph so the previous per-cell glyph is cleared.
  - Shader/backend note: existing Vulkan and Metal foreground paths already size quads from `glyph_size_x`, so no shader change was required.
- [x] Implement ligature-aware shaping and query support in the font layer, with `enable_ligatures = true/false` plumbed through config and render-test options.
- [x] Update the grid rendering pipeline to combine eligible two-cell neighbors, redraw broken ligatures when either side changes, and leave double-width terminal cells alone.
- [x] Add unit coverage for config parsing, ligature enable/disable shaping, and grid pipeline leader/continuation behavior.
- [x] Add a dedicated ligature render snapshot scenario with a ligature-heavy source line.
- [x] Run `ctest --test-dir build --build-config Release --output-on-failure`.

## Outcome

- Added `enable_ligatures` to `AppConfig`, `AppOptions`, and render-test scenarios.
- Added a ligature query to `TextService` and HarfBuzz feature disabling when ligatures are turned off.
- Updated the grid rendering pipeline to expand dirty neighbors, shape eligible two-cell clusters together, and send a blank continuation update for the second cell.
- Added a bundled `CascadiaCode-Regular.ttf` ligature test font plus `ligatures-view` render coverage, while pinning the older baseline scenarios to `enable_ligatures = false` so unrelated snapshots stay stable.

## Notes

The initial review notes expected backend shader changes, but the current renderer already emits foreground quads from atlas glyph width. The implementation stayed in the font/config/pipeline layers instead.

## Sub-Agent Split

- One agent on font shaper changes (HarfBuzz ligature cluster → cell mapping).
- One agent on renderer / shader changes (wide leader quad in Vulkan + Metal).
