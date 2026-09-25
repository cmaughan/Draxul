# Invalidate every glyph-atlas consumer after overflow
**Severity:** HIGH  
**Source:** Codex #7; `libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp:303`.

An overflowing pane consumes the shared atlas reset and rebuilds only itself, leaving other panes with obsolete glyph coordinates.

**Investigation**

- [x] Identify all shared atlas consumers and their update ordering, including chrome and hidden grids.

**Fix strategy**

- [x] Introduce generation-aware invalidation so every dependent consumer rebuilds before using replaced atlas contents.
- [x] Handle resets occurring after another consumer has already been processed.

**Acceptance criteria**

- [x] Overflow triggered by one pane preserves correct text in clean and subsequently revealed panes without manual test invalidation.
- [ ] Run core aggregate tests, relevant multi-pane render checks, and same-cache smoke on supported backends.

## Implementation notes

- The atlas now exposes a non-consuming generation. Each grid pipeline compares
  its last rendered generation on every flush, so a clean or hidden grid is
  rebuilt when it next participates; a reset during its own shaping causes a
  retry before publishing cell updates.
- After host pumping, App asks every pane host in every tab and Space to repair
  cached atlas coordinates when the generation changes. This covers hidden
  panes without relying on new terminal/Neovim data. A later reset during
  chrome or overlay drawing requests another frame, where the repair occurs
  before presentation. Chrome, palette, toast, and plugin overlay helpers
  resolve text during their draw path rather than retaining grid cell UVs.
- A bounded repair pass that still overflows postpones presentation to the
  next pump rather than showing stale grid UVs. The generation check marks
  cells dirty without forcing one full atlas upload per pane; the atlas itself
  tracks the reset's dirty region and uploads it once when needed.
- Multi-host tests now cover an initially clean hidden grid after another pane
  overflows and a previously processed pane after a later pane resets. The fake
  atlas repacks glyph UVs across generations, so these tests assert the new
  coordinates rather than merely counting updates. Windows validation is
  recorded below; live multi-pane and Metal checks remain.

**Windows validation (2026-09-25):** Core + all product aggregate passed
49/49 CTest entries in 238.19 seconds. The final strengthened `[multi-host]`
selection passed 6 cases and 46 assertions, including repacked UV checks for
clean hidden and previously flushed panes. `py do.py unicode` passed the Vulkan
snapshot (0.0195% changed pixels, within threshold). The standard same-cache
smoke wrapper timed out at its fixed 30 seconds while restoring the existing
nine-pane Session; the identical Debug executable and wrapper environment
passed with a 90-second bound in roughly 45 seconds. A live multi-pane overflow
inspection and macOS/Metal render and smoke checks remain open, so the
cross-backend acceptance box stays unchecked.
The Windows Vulkan `py do.py panel` snapshot also passed on 2026-09-25
(2.5385% changed pixels, below its 18% threshold); this exercises native
panel/chrome text composition but does not deliberately overflow a live
multi-pane atlas.
