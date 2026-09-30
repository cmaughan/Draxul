# Use the glyph-atlas interface for overlay layout

**Priority:** P2 — layout tests currently need real font initialization.  
**Source:** `libs/draxul-gui/src/palette_renderer.cpp`  
**Proposed by:** Codex 12. **Owner:** one GUI/text agent.  
**Evidence:** palette/toast layout and occlusion use `TextService` for cluster resolution; `IGlyphAtlas` already exposes it, while tests skip when fonts are unavailable.

**Boundary verification**
- [ ] Trace cluster style and atlas-reset ordering through palette, toast and occlusion helper.
**Implementation and migration**
- [ ] Accept `IGlyphAtlas&` with explicit regular style; leave tooltip bitmap work on concrete text service.
**Unit tests**
- [ ] Fake-atlas clipping, continuation-cell and color cases; retain real-font integration.
**Cross-platform validation**
- [ ] Check font rendering on Windows/macOS, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Document interface use and reset sequencing.
**Acceptance criteria**
- [ ] Overlay layout can be tested without loading a font; atlas behavior remains intact.
