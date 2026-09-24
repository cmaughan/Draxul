# Invalidate every glyph-atlas consumer after overflow
**Severity:** HIGH  
**Source:** Codex #7; `libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp:303`.

An overflowing pane consumes the shared atlas reset and rebuilds only itself, leaving other panes with obsolete glyph coordinates.

**Investigation**

- [ ] Identify all shared atlas consumers and their update ordering, including chrome and hidden grids.

**Fix strategy**

- [ ] Introduce generation-aware invalidation so every dependent consumer rebuilds before using replaced atlas contents.
- [ ] Handle resets occurring after another consumer has already been processed.

**Acceptance criteria**

- [ ] Overflow triggered by one pane preserves correct text in clean and subsequently revealed panes without manual test invalidation.
- [ ] Run core aggregate tests, relevant multi-pane render checks, and same-cache smoke on supported backends.
