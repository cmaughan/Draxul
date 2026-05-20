# Bug: FT_Face Lifetime Hazard in GlyphCache

**Type:** bug
**Priority:** 0 (highest — silent use-after-free)
**Source:** Claude review (A6 strong agreement with Gemini)

## Problem

`libs/draxul-font/src/glyph_cache.cpp` stores a raw `FT_Face` pointer obtained from `TextService`.
When the user changes the font size, `TextService::reinitialize()` (or equivalent) frees and recreates the `FT_Face`.
If `GlyphCache` is not invalidated before the old face is freed, any subsequent call to render a glyph that hits the cache will use a dangling pointer — undefined behaviour with no diagnostic.

Key files:
- `libs/draxul-font/src/glyph_cache.cpp` — stores the raw `FT_Face*`
- `libs/draxul-font/include/draxul/text_service.h` — owns the `FT_Face` lifetime
- `app/app.cpp` — calls font size change + requests redraw; look for the cascade order

## Investigation steps

- [x] Read `glyph_cache.cpp` and identify exactly where and how `FT_Face*` is stored.
- [x] Read `text_service.h` / `text_service.cpp` and trace what happens to the old face on reinit.
- [x] Confirm the ordering in `App::apply_font_metrics()` (or equivalent): does `GlyphCache` get cleared *before* or *after* the old face is freed?
- [x] Check whether `GlyphCache::clear()` exists and is called on font change.

## Fix strategy

Choose **one** of:

1. **Generation counter (preferred)**: add a `uint32_t generation_` to `TextService`; increment on reinit. `GlyphCache` stores the last-seen generation and self-invalidates on mismatch at the top of every public method.
2. **Invalidation callback**: `TextService` notifies registered callbacks (including `GlyphCache`) before freeing the face; `GlyphCache::invalidate()` drops all cached glyphs and the stale pointer.
3. **Shared ownership**: `TextService` wraps the face in a ref-counted handle; `GlyphCache` holds a copy of the handle. Old glyphs stay valid until they naturally fall out of the cache.

Option 1 is the smallest change. Option 3 is the safest for concurrent extensions.

## Acceptance criteria

- [x] A font-size change no longer leaves `GlyphCache` holding a freed `FT_Face*`.
- [x] After the fix, a font-size change followed by rendering produces correct glyphs.
- [x] No `ASAN` use-after-free error when running with the `mac-asan` preset and changing font size mid-session.
- [x] Add a regression note or assertion that fires if the face is accessed after being freed.

## Interdependencies

- `11 fontresolver-style-detection -test`: add a regression test covering font-size change + glyph render after this fix lands.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
