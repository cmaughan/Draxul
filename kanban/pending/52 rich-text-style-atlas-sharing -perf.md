# Share rich-text services across style variants

**Source:** `libs/draxul-font/src/rich_text_service.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. Lines 62–87 key a full `TextService` by size and bold/italic, while style flags are already passed at use. Each service owns a 2048² RGBA CPU atlas (`glyph_cache.cpp:172–178`) and can cause a corresponding GPU atlas.

- [ ] **Baseline:** Count services, CPU/GPU atlases, RSS, and VRAM for a mixed-style Markdown or Kanban preview.
- [ ] **Implement:** Share by compatible point size while passing style at shaping/rasterization and maintaining correct atlas identity.
- [ ] **Functional safety:** Check metrics, fallback faces, bold/italic glyphs, atlas IDs, font reset, and upload generations.
- [ ] **Compare:** Report atlas count and memory before/after with identical text rendering.
- [ ] **Platforms:** Verify mixed-style output on Vulkan and Metal, aggregate and smoke.
- [ ] **Acceptance:** Style variants do not each reserve a complete redundant atlas.
