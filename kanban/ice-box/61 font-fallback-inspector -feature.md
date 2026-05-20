# 61 Font Fallback Inspector

## Why This Exists

When a character renders unexpectedly (wrong font, missing glyph, wrong width), there is no way to inspect which font was selected for a given cell. The debug panel shows atlas stats but not per-cell font attribution. This makes font configuration debugging opaque.

Identified by: **GPT** (QoL #6).

## Goal

Add a font inspector mode to the debug panel that, when a cell is hovered or selected, shows: the cell's codepoint, the font face selected by `FontSelector`, the glyph ID, the atlas region, and the advance width.

## Implementation Plan

- [ ] Read `libs/draxul-font/include/draxul/text_service.h` for the API to query per-glyph font selection.
- [ ] Read `app/app.cpp` for mouse-hover events and debug panel integration.
- [ ] Add a `TextService` query method `get_glyph_debug_info(uint32_t codepoint) -> GlyphDebugInfo` returning face name, glyph ID, atlas region, advance.
- [ ] In the debug panel, add a "Font Inspector" section.
- [ ] Track the hovered grid cell from mouse position (already computed for mouse input routing).
- [ ] On hover: call `get_glyph_debug_info(hovered_cell.codepoint)` and display in the panel.
- [ ] Guard behind `DRAXUL_ENABLE_DEBUG_PANEL` compile flag (item 55).
- [ ] Run `ctest` and `clang-format`.

## Sub-Agent Split

Single agent. Mostly additive; no existing code paths need changing.
