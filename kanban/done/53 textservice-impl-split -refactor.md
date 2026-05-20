# 53 TextService Impl Split

## Why This Exists

`TextService::Impl` in `libs/draxul-font/src/text_service.cpp` is a dense, all-in-one implementation handling: primary font resolution, fallback font discovery, font selection caching, ligature span detection, atlas reset policy, glyph raster fallback, and emoji/color preference heuristics. It is a hotspot for parallel-work conflicts and regressions — any change to ligature logic risks touching fallback logic, and vice versa.

Identified by: **GPT** (finding #8), **Claude** (TextService::Impl dense, bad things), **Gemini** (font service concentrated, hotspot).

## Goal

Split `TextService::Impl` into focused sub-objects with clear ownership:
- `FontResolver` — discovers and loads font faces (primary + fallbacks)
- `FontSelector` — selects the right face for a given codepoint (with cache)
- `GlyphAtlasManager` — manages atlas packing, rasterisation, and reset policy
- `LigatureAnalyser` — detects eligible ligature spans in a cluster sequence

`TextService::Impl` becomes a thin coordinator of these four.

## Implementation Plan

- [x] Read `libs/draxul-font/src/text_service.cpp` in full to map which code belongs to which concern.
- [x] Extract `FontResolver` first (no dependencies on the others) — font loading and fallback candidate discovery.
- [x] Extract `GlyphAtlasManager` (depends on `FontResolver` for rasterisation).
- [x] Extract `FontSelector` (depends on `FontResolver`, uses `GlyphAtlasManager` for rasterise-on-demand).
- [x] Extract `LigatureAnalyser` (depends only on `FontSelector` for width queries).
- [x] `TextService::Impl` coordinates the four: shape request → `FontSelector` → `LigatureAnalyser` → `GlyphAtlasManager`.
- [x] All files remain in `libs/draxul-font/src/` (private to the library).
- [x] Run `ctest` and render snapshots to verify no visual regression.
- [x] `clang-format` all touched files.

## Sub-Agent Split

Two agents possible after the extraction plan is agreed: one extracts `FontResolver` + `GlyphAtlasManager`, another extracts `FontSelector` + `LigatureAnalyser`. Coordinate on the shared header before starting.
