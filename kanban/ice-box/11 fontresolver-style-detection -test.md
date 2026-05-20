# Test: FontResolver Style-Font Auto-Detection Heuristics

**Type:** test
**Priority:** 11
**Source:** Gemini review

## Problem

`FontResolver` (in `libs/draxul-font/src/font_resolver.h`) uses filename-based heuristics to locate bold/italic/bold-italic font variants from a base font path (e.g. appending `-Bold`, `-Italic`, `-BoldItalic`). There are currently no tests for this logic.

This means:
- Fonts that don't follow the naming convention silently fall back without the user knowing.
- A regression in the heuristic (e.g. wrong suffix order) produces wrong font style rendering with no diagnostic.

**Note:** Do alongside `18 fontresolver-header-extraction -refactor` — the extraction moves the implementation to a `.cpp` file which makes it directly testable.

## Investigation steps

- [ ] Read `libs/draxul-font/src/font_resolver.h` — find the style heuristic logic.
- [ ] Identify the full list of naming conventions tried (e.g. `-Bold`, `Bold`, `_Bold`, uppercase variants).
- [ ] Check whether `FontResolver` is currently tested anywhere in `tests/`.
- [ ] Understand the fallback: what happens when no bold variant is found?

## Test design

Add to `tests/font_resolver_tests.cpp` (create if needed). Use test font files in `tests/fixtures/` or the existing fonts in `fonts/`.

### Naming convention detection

- [ ] Given a base font path `fonts/MonoFont-Regular.ttf`, assert the resolver finds `MonoFont-Bold.ttf` if present.
- [ ] Given a base font path `fonts/MonoFont.ttf`, assert reasonable fallback behaviour when `MonoFont-Bold.ttf` does not exist.
- [ ] Test the full matrix: bold, italic, bold-italic, each with/without a matching file.

### Fallback behaviour

- [ ] When bold variant is not found, assert the regular face is used as fallback and a `WARN` is logged.
- [ ] Verify no crash when all four variants are absent.

### Font-size-change regression (covers bug `00`)

- [ ] Load a font, render a glyph, change the font size, render the same glyph again.
- [ ] Assert: the second render uses the new face and does not crash (covers the FT_Face lifetime fix).

## Acceptance criteria

- [ ] All above tests pass under ASan.
- [ ] Tests are part of `draxul-tests` and do not require a real display.

## Interdependencies

- **`18 fontresolver-header-extraction -refactor`**: extraction makes the code directly unit-testable; do refactor first.
- **`00 ftface-lifetime-hazard -bug`**: the font-size-change regression test in this file covers that bug.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
