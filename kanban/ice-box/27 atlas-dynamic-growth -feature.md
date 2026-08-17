# WI 27 — Dynamic glyph atlas growth (multi-page atlas)

**Type:** feature  
**Source:** review-latest.claude.md  
**Consensus:** review-consensus.md Phase 7

---

## Goal

When the 2048×2048 glyph atlas fills up, allocate a bounded second page rather than
performing a disruptive reset/retry cycle.

---

## Current behaviour

Atlas overflow is typed and observable. `GlyphAtlasManager` resets the atlas and retries
once, rate-limits the warning, and exposes reset statistics; deterministic overflow and
multi-host reset coverage exists. The remaining gap is avoiding cache-wide invalidation
for workloads with large Unicode/font vocabularies.

---

## Implementation Plan

**Phase A — characterize:**
- [x] Pin overflow, reset/retry, warning, and multi-host behavior in tests.
- [ ] Determine the maximum number of atlas pages the GPU binding supports (Metal/Vulkan texture array vs separate bindings).

**Phase B — multi-page atlas:**
- [ ] Extend `GlyphCache` to support multiple atlas pages (e.g. a `std::vector<ShelfPacker>`).
- [ ] When the current page is full, allocate a new page (up to a configurable `max_atlas_pages` — default 4).
- [ ] Update `AtlasRegion` to carry a page index alongside UV coordinates.
- [ ] Update the Metal and Vulkan shader bindings to accept a texture array or indexed textures.
- [ ] Update `grid_rendering_pipeline.cpp` upload logic to handle per-page dirty regions.

**Phase C — user feedback:**
- [ ] When the page limit is reached, retain the current rate-limited warning and add a user-facing diagnostic only if glyph loss remains possible.
- [ ] Replace the reset statistic with page-count/capacity data in the F12 diagnostics overlay.

**Phase D — tests:**
- [ ] Extend the existing atlas-overflow suites to verify growth and no glyph loss.
- [ ] Verify render-snapshot tests still pass after the shader changes.

---

## Notes for the agent

- This touches the GPU shader layer (both Metal and Vulkan) — coordinate with the renderer parity work.
- The `AtlasRegion` struct change is a breaking change for all callers; audit before landing.
- Start with Metal (macOS) as it's the primary dev platform.

---

## Interdependencies

- Characterization is complete in `kanban/done/17 atlas-exhaustion -test.md`.
- Atlas upload deduplication is already delivered; no historical bare-number blocker remains.
- Existing multi-host atlas dirty coverage should be revisited after dynamic growth lands.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
