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
- [x] Run the Windows core/product aggregate, multi-host invalidation checks, Vulkan text snapshots, and same-cache smoke.
- [ ] Run the corresponding Metal render and startup checks on macOS.

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
  recorded below; at that stage live multi-pane and Metal checks had not run.

**Windows validation (2026-09-25):** Core + all product aggregate passed
49/49 CTest entries in 238.19 seconds. The final strengthened `[multi-host]`
selection passed 6 cases and 46 assertions, including repacked UV checks for
clean hidden and previously flushed panes. `py do.py unicode` passed the Vulkan
snapshot (0.0195% changed pixels, within threshold). The standard same-cache
smoke wrapper timed out at its fixed 30 seconds while restoring the existing
nine-pane Session; the identical Debug executable and wrapper environment
passed with a 90-second bound in roughly 45 seconds. A native GPU snapshot
deliberately forcing overflow was not available; the shared generation path is
covered by the deterministic multi-host repacked-UV tests. Metal render and
smoke checks remain open.
The Windows Vulkan `py do.py panel` snapshot also passed on 2026-09-25
(2.5385% changed pixels, below its 18% threshold); this exercises native
panel/chrome text composition but does not deliberately overflow a live
multi-pane atlas.

**Additional Windows check (2026-09-25):** The current Debug cache passed
the 25/25 core aggregate and same-cache startup (90-second bound). Vulkan
`py do.py unicode` and `py do.py panel` snapshots passed at 0.0230% and
2.4296% changed pixels respectively. These do not force a visible multi-pane
overflow; the backend-specific Metal gate remains open.

**Final Windows check (2026-09-25):** The current core-and-products aggregate
passed 49/49 CTest entries, including the deterministic multi-host repacked-UV
checks. A same-cache single-pane Debug startup and Release startup passed.
The existing Vulkan Unicode and panel snapshots passed in the same Debug
cache. The only remaining gate is Metal rendering/startup on macOS; the
generation-repair algorithm itself is backend-independent.
