# Ligature with Mid-Frame Atlas Reset Test

**Type:** test
**Priority:** 16
**Raised by:** Claude

## Summary

Add a test that verifies the glyph atlas handles a mid-frame reset correctly when ligature glyphs are present. Specifically: after an atlas reset triggered by atlas exhaustion or font change, ligature glyph IDs must be re-rasterised and re-cached correctly, with no stale atlas regions returned.

## Background

The glyph atlas is a shelf-packed 2048×2048 texture. If the atlas fills completely (possible with large Unicode scripts or many distinct ligatures), it must be reset and all glyphs re-rasterised on demand. Ligature glyphs are multi-cell shapes that occupy a wider atlas region than single-cell glyphs. If the atlas reset logic does not correctly invalidate all cached regions (including ligature entries that span multiple cell widths), subsequent renders may display garbage or blank cells where ligatures should appear.

## Implementation Plan

### Files to modify
- `tests/ligature_atlas_reset_tests.cpp` — created

### Steps
- [x] Write test: cache a set of single-cell glyphs; trigger atlas reset; verify re-query returns valid (different or equal) atlas regions
- [x] Write test: cache a ligature glyph (multi-cell width); trigger atlas reset; verify re-query returns valid atlas region with correct dimensions
- [x] Write test: fill atlas to near-capacity with synthetic glyphs; add one more glyph that triggers reset; verify all previous glyph IDs now resolve to stale/invalid regions (triggering re-rasterisation on next query)
- [x] Write test: font-size change triggers atlas clear; subsequent glyph queries rasterise at new size
- [x] Register with ctest (added to draxul-tests in tests/CMakeLists.txt)

## Depends On
- None

## Blocks
- None

## Notes
The test may require access to the `GlyphCache` internals to trigger an atlas reset directly without actually filling the 2048×2048 atlas. Consider adding a `reset_for_testing()` method or a reduced-capacity test mode to `GlyphCache`.

> Work item produced by: claude-sonnet-4-6
