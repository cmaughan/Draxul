# Share NanoVG CPU batch construction

**Priority:** P1 — duplicated fill/stroke/triangle geometry risks backend drift.  
**Source:** `libs/draxul-nanovg/backend/src/nanovg_vk.cpp`  
**Proposed by:** Claude 20; Codex 4. **Owner:** one NanoVG agent across both backends.  
**Evidence:** Vulkan and Metal backends repeat fan expansion, path offsets, cover geometry and uniform decisions; paint conversion is already shared.

**Boundary verification**
- [ ] Record call order, uniform layout, stencil and Metal pre-sizing invariants.
**Implementation and migration**
- [ ] Add backend-private neutral batch records; move triangles, stroke, then fill. Leave blend, texture, resources and encoding native.
**Unit tests**
- [ ] Device-free geometry, offsets, uniforms and ordering in a narrow NanoVG target.
**Cross-platform validation**
- [ ] Check Vulkan and Metal render snapshots, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Document CPU batch versus native encoder ownership.
**Acceptance criteria**
- [ ] Both backends consume one CPU batch builder with unchanged visual output.
