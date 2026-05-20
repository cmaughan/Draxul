# WI 35 — font-fallback-corpus

**Type:** test  
**Priority:** Medium  
**Source:** review-consensus.md §5c — Gemini  
**Produced by:** claude-sonnet-4-6

---

## Problem

The font fallback chain (primary → fallbacks → last-resort) is exercised only incidentally by existing tests. A comprehensive corpus test across diverse Unicode ranges is missing. Specific risks:

1. A codepoint not covered by any fallback font triggers an infinite loop or assertion in `GlyphCache::rasterize_cluster`.
2. Right-to-left or bidirectional text causes incorrect shaping or a crash in HarfBuzz.
3. Combining characters or zero-width joiners produce a glyph wider than its cell allocation, corrupting adjacent cells.
4. Emoji with skin-tone modifiers (multi-codepoint sequences) produce incorrect glyph count.
5. Failed rasterizations are not cached, so a missing glyph in an animated neovim screen causes repeated FreeType calls each frame (performance regression).

This is distinct from icebox WI 61 (font-fallback-inspector), which is a user-facing UI feature. This WI is a test harness only.

---

## Investigation

- [x] Read `libs/draxul-font/src/glyph_cache.cpp` — understand `rasterize_cluster`: what it does when a glyph has no coverage in any font, and whether failed lookups are cached.
- [x] Read `libs/draxul-font/include/draxul/text_service.h` — find the fallback chain API.
- [x] Check whether existing tests exercise any non-ASCII, non-BMP, or RTL input.
- [x] Identify which test font(s) are staged in the build — they may not have broad Unicode coverage; the test may need to use a system font or a bundled Noto font.

---

## Test Cases to Implement

### Case 1: Basic Latin + extended Latin (U+0020–U+024F)
- Shape and rasterize a sentence containing accented Latin characters.
- Assert no crash, result has > 0 glyphs, all widths are positive.

### Case 2: CJK Unified Ideographs (U+4E00–U+9FFF, sample of 100)
- Shape a string of CJK characters.
- Assert no crash; if no CJK font is available, assert a placeholder (tofu/replacement) is returned — not a crash.

### Case 3: Emoji — basic (U+1F600–U+1F64F)
- Shape a string of 10 basic emoji.
- Assert no crash; at least one glyph per emoji (or a tofu block), width matches cell count.

### Case 4: Emoji with skin-tone modifier (ZWJ sequence)
- Shape e.g. `👨‍💻` (U+1F468 + ZWJ + U+1F4BB).
- Assert no crash; result is either a single composite glyph or a two-glyph sequence.

### Case 5: RTL characters (Arabic, U+0600–U+06FF)
- Shape a short Arabic word.
- Assert no crash and result has > 0 glyphs.

### Case 6: Combining marks (U+0300–U+036F)
- Shape `e` + combining grave accent (`è`).
- Assert single glyph or correct two-glyph cluster; assert width does not exceed one cell for a standard terminal font.

### Case 7: Missing glyph / null result caching
- Shape a codepoint that is guaranteed to be absent from all configured fonts (e.g., a private-use area codepoint with no font mapping).
- Assert no crash.
- Shape the same codepoint a second time.
- Assert the second call is faster (cached miss) — or at minimum does not crash.

### Case 8: Large corpus stress
- Shape 500 random BMP codepoints in sequence.
- Assert no crash, no timeout, atlas space usage is within atlas bounds.
- This also feeds information to WI 17 (atlas-exhaustion) and WI 27 (atlas-dynamic-growth).

---

## Implementation Notes

- Tests should live in `tests/font_fallback_corpus_test.cpp`.
- If the test font used in CI doesn't have broad Unicode coverage, the test should use a system font (e.g., Menlo on macOS, DejaVu Sans Mono on Linux) or skip cases that require unavailable font coverage rather than hard-failing.
- Case 7 (missing glyph caching) may require exposing a test hook in `GlyphCache` to check whether a cluster is in the cache.
- A sub-agent can do this work in parallel with WI 33 and WI 34 — no shared code paths.

---

## Acceptance Criteria

- [x] All 8 cases pass under `ctest` on macOS (CI baseline).
- [ ] No crashes under ASan for any of the 8 cases.
- [x] Case 8 completes in < 5 seconds on a developer machine.
- [x] Smoke test passes.
