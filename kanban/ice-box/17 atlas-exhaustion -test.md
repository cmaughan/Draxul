# WI 17 — Glyph atlas exhaustion test

**Type:** test  
**Source:** review-latest.claude.md  
**Consensus:** review-consensus.md Phase 5

---

## Goal

Characterise and verify the glyph cache's behaviour when the 2048×2048 atlas fills up. Currently the exhaustion path has no test and silently drops glyphs. This test first characterises the existing behaviour, then (once WI 27 implements dynamic growth) verifies the improved path.

---

## What to test

**Phase A — characterise current behaviour (write now):**
- [ ] Fill the glyph cache with >10K unique glyphs (e.g. bulk-rasterise Latin + CJK + emoji codepoints).
- [ ] Assert the cache reports a full condition (`rasterize_cluster` returns a sentinel or logs an error).
- [ ] Assert the renderer does not crash or produce out-of-bounds atlas coordinates.
- [ ] Document the current degradation: which glyphs disappear, what the atlas looks like, whether any diagnostic counter is updated.

**Phase B — verify graceful degradation (update after WI 27):**
- [ ] After atlas grows to a second page, assert previously-loaded glyphs remain intact.
- [ ] Assert a toast notification is emitted when an emergency eviction occurs.
- [ ] Assert render output still passes a snapshot comparison after exhaustion + growth.

---

## Implementation notes

- This can be a headless test using the `FakeRenderer` fixture (see WI 25) or the actual Metal/Vulkan headless init (see WI 14 icebox `metal-headless-init`).
- Focus Phase A on the `GlyphCache` / `TextService` layer — most of the logic is platform-independent.
- Run under ASan to catch any buffer overflows in the shelf-packing logic.
- Place in `tests/atlas_exhaustion_test.cpp`.

---

## Interdependencies

- WI 109 (atlas-upload-dedup, active) should land before or alongside this — the test will be cleaner once upload ownership is unified.
- WI 108 icebox (atlas-dirty-multi-subsystem) is a related test covering the dirty-tracking path.
- WI 27 (atlas-dynamic-growth feature) depends on this test characterising the failure mode first.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
