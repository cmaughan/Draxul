# Extract common Markdown upload and batch planning

**Summary:** Share the preparation of Markdown drawing data so Windows and Mac use the same common checks while keeping any intentional platform differences explicit.

**Priority:** P1 — duplicated planning currently has unexplained backend differences.  
**Source:** `modules/markdown/draxul-markdown/src/markdown_render_pass_vk.cpp`  
**Proposed by:** Claude 21. **Owner:** one Markdown rendering agent. **Related:** card 21.  
**Evidence:** Vulkan clamps some upload rectangles and maximizes revision; Metal rejects and assigns; both duplicate range validation and push constants.

**Boundary verification**
- [ ] Characterize both existing rectangle, sampler and revision policies before moving code.
**Implementation and migration**
- [ ] Add CPU plan records for shared upload layout, atlas generation and validated batches; keep policy differences explicit and native resource/encoding work in each pass.
**Unit tests**
- [ ] Plain-data short-buffer, rectangle, revision and instance-range cases in Markdown layout/plan target.
**Cross-platform validation**
- [ ] Preserve both Vulkan and Metal output; run Markdown renders, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Add shader ABI assertions and record unresolved policy differences.
**Acceptance criteria**
- [ ] Common mechanics have one owner without silently changing backend behavior.
