# 46 Font Fallback Corpus Test

## Why This Exists

The font fallback chain (primary → emoji → system fallbacks) has no corpus-level test. It is only exercised by the unicode render snapshots for a small set of characters. A regression in fallback selection (e.g., a CJK character falling through to the wrong font, or an emoji rendered as text) would only be caught if it happened to affect one of the blessed scenario characters.

Identified by: **Gemini** (tests #2).

## Goal

Add a parameterised test that attempts to shape/rasterise a representative string from each of 20+ Unicode script blocks through `TextService`, verifying no assertion, no empty glyph, and no crash.

## Implementation Plan

- [x] Read `libs/draxul-font/include/draxul/text_service.h` and its `Impl` to understand the `shape()` / `get_glyph()` API.
- [x] Read `tests/` for existing font/text tests.
- [x] Write `tests/font_fallback_corpus_tests.cpp`:
  - Initialise `TextService` with the default font config (or a test font + Noto fallback).
  - For each script block sample (Latin, Greek, Cyrillic, Hebrew, Arabic, Devanagari, CJK Unified, Hangul, Hiragana, Katakana, Thai, Emoji, Math symbols, Box drawing, Braille, Musical notation):
    - Call `TextService::shape(sample_string)` and verify at least one glyph is returned.
    - Verify the returned glyph has non-zero advance width.
    - Verify no assertion fires.
  - Report which script blocks produce missing-glyph fallback (.notdef) as warnings, not failures.
- [x] Wire into `tests/CMakeLists.txt`.
- [x] Run `ctest` and `clang-format`.

## Sub-Agent Split

Single agent. Read-only test; no production code changes.
