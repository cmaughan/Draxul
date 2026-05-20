---
# WI 80 — Multi-Cell Ligature Support (>2 Cells)

**Type:** feature  
**Priority:** medium (most significant functional gap in the text engine)  
**Raised by:** [G] Gemini (top QoL), [C] Claude (implied — notes 2-cell limit in docs)  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

The `GridRenderingPipeline` currently supports only 2-cell ligature combinations. Common programming ligatures like `===`, `!==`, `>>=`, `/***/`, `<<=`, and `...` span 3 or more cells. Users using these ligatures see either partial rendering (only 2-cell pairs recognised) or fallback to non-ligature glyphs.

---

## Investigation Steps

- [x] Read the ligature code path in `libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp` (or wherever 2-cell ligature detection lives)
- [x] Read `libs/draxul-font/src/text_service.cpp` — the HarfBuzz shaping call to understand how glyph IDs map to ligature outputs
- [x] Identify the exact cut-off: where is `kMaxLigatureCells = 2` (or equivalent) enforced?
- [x] Check `docs/features.md` — confirms ligature support exists but 2-cell limit is not documented

---

## Design

HarfBuzz already shapes multi-glyph ligatures — the limit is in how the pipeline builds the shaping input buffer. The fix is to:
1. Increase the lookahead window from 2 cells to N cells (N = 4 or 6 is practical)
2. Feed more cells into HarfBuzz's shaping buffer in a single call
3. Handle the case where a 3+ cell ligature crosses a highlight-change boundary (must break the ligature at highlight changes)

---

## Implementation Steps

- [x] Locate the ligature window constant and increase it to 4 or 6 cells
- [x] Update the shaping loop to build an N-cell input buffer, not a 2-cell one
- [x] Handle highlight boundary: if highlight changes within the N-cell window, break the ligature at that boundary (existing 2-cell code likely does this — verify and extend)
- [ ] Update render test scenarios: add `tests/render-scenarios/ligature_3cell.toml` with `===`, `!==`, `>>=` inputs (deferred — requires full GUI for render test blessing)
- [ ] Bless the new reference BMP with `py do.py blessligatures` (deferred — requires full GUI)

---

## Acceptance Criteria

- [x] `===`, `!==`, `>>=` render as single ligature glyphs (where the font supports them) — code path supports up to 6-cell ligatures; visual verification deferred to render test blessing
- [x] No regression in existing 2-cell ligature render tests — all existing tests pass
- [x] Highlight boundary correctly breaks ligatures (a 3-cell `===` with a highlight change on the second `=` renders as two separate glyphs) — unit test added and passing
- [x] `enable_ligatures = false` still suppresses all ligatures — existing test covers this

---

## Notes

**Subagent recommended** — touches the font shaping pipeline, the grid rendering pipeline, and the render test framework. Requires platform-specific render test blessing on both Windows and macOS.

**Render test caveat**: ligature rendering is font-dependent. Tests should use JetBrains Mono Nerd Font (bundled), which includes multi-cell ligatures.
