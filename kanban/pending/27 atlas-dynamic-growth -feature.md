# WI 27 — Dynamic glyph atlas growth (multi-page atlas)

**Type:** feature  
**Source:** review-latest.claude.md  
**Consensus:** review-consensus.md Phase 7

---

## Goal

When the 2048×2048 glyph atlas fills up, allocate a second atlas page rather than silently dropping new glyphs. Show a diagnostic toast when an emergency eviction occurs.

---

## Current behaviour

`GlyphCache::rasterize_cluster()` returns a failure sentinel (or logs ERROR) when the shelf-packer has no room. The renderer silently renders blank cells for overflowed glyphs. No user notification. No dynamic reallocation. Users with large Unicode vocabularies (CJK + emoji + many fonts) can hit this in real workflows.

---

## Implementation Plan

**Phase A — characterise (prerequisite: WI 17):**
- [ ] Review WI 17 test results to understand the real exhaustion threshold and failure mode.
- [ ] Determine the maximum number of atlas pages the GPU binding supports (Metal/Vulkan texture array vs separate bindings).

**Phase B — multi-page atlas:**
- [ ] Extend `GlyphCache` to support multiple atlas pages (e.g. a `std::vector<ShelfPacker>`).
- [ ] When the current page is full, allocate a new page (up to a configurable `max_atlas_pages` — default 4).
- [ ] Update `AtlasRegion` to carry a page index alongside UV coordinates.
- [ ] Update the Metal and Vulkan shader bindings to accept a texture array or indexed textures.
- [ ] Update `grid_rendering_pipeline.cpp` upload logic to handle per-page dirty regions.

**Phase C — user feedback:**
- [ ] When the page limit is reached (all pages full), emit a toast warning: "Glyph atlas full — some glyphs may not render correctly."
- [ ] Add an atlas-page-count stat to the F12 diagnostics overlay.

**Phase D — tests:**
- [ ] Update WI 17 (atlas exhaustion test) Phase B scenario to verify growth and no glyph loss.
- [ ] Verify render-snapshot tests still pass after the shader changes.

---

## Notes for the agent

- This touches the GPU shader layer (both Metal and Vulkan) — coordinate with the renderer parity work.
- The `AtlasRegion` struct change is a breaking change for all callers; audit before landing.
- Start with Metal (macOS) as it's the primary dev platform.

---

## Interdependencies

- **Requires WI 17 (atlas exhaustion test)** to characterise the failure before implementing the fix.
- WI 109 (atlas-upload-dedup, active) should land first to simplify the dirty-tracking extension.
- WI 108 icebox (atlas-dirty-multi-subsystem test) should be updated alongside this.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
