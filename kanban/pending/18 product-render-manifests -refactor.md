# Let products own render scenarios and references

**Summary:** Let each plugin keep its own visual test definitions and reference images so its tests can be maintained independently and disappear when the plugin is disabled.

**Priority:** P2 — every product currently edits the root render manifest.  
**Source:** `tests/render/manifest.json`  
**Proposed by:** Claude 39. **Owner:** one core registration agent, then product owners. **Depends on:** card 02 scope metadata.  
**Evidence:** root manifest holds product scenarios; CMake and `do.py` assume root paths. MegaCity/ScoreView CTests lack product target gating.

**Boundary verification**
- [ ] Inventory scenario names, paths, platforms, required targets, scopes and bless commands.
**Implementation and migration**
- [ ] Add product manifest/root registration; update CMake and `do.py` scenario resolution; migrate products in separate commits.
**Unit tests**
- [ ] Test global name uniqueness, disabled/uninitialized products, paths, CTest inventory and command mapping.
**Cross-platform validation**
- [ ] Preserve Windows/macOS goldens and platform filters; run affected render cases, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Update product render guidance and bless entry points.
**Acceptance criteria**
- [ ] A disabled product registers no product render test; core no longer owns its scenario assets.
