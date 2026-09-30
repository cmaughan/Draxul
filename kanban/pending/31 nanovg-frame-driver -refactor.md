# Share NanoVG pass frame-driving sequence

**Priority:** P2 — core and plugin adapters repeat begin/scissor/translate/draw/end behavior.  
**Source:** `plugins/support/nanovg/src/plugin_nanovg_pass_internal.h`  
**Proposed by:** Claude 41, narrowed. **Owner:** one NanoVG adapter agent. **Related:** card 24.  
**Evidence:** core Vulkan/Metal passes and plugin NanoVG pass repeat frame sequence; plugin already has operation hooks, while native allocator ownership differs.

**Boundary verification**
- [ ] Pin viewport origin, callback consumption, preparation failure and teardown order.
**Implementation and migration**
- [ ] Add neutral frame driver inside backend support; migrate core/plugin adapters, retaining native resource and ABI translation.
**Unit tests**
- [ ] Test nonzero origin, failed preparation, draw order and callback lifetime via operation hooks.
**Cross-platform validation**
- [ ] Preserve Vulkan prepass and Metal encoder/drawable lifetime; run renders, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Document shared sequence versus native preparation.
**Acceptance criteria**
- [ ] Both adapters use one frame sequence without changing allocator or encoder ownership.
