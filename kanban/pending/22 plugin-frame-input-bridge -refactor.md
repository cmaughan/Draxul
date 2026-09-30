# Share plugin frame mapping and primitive input translation

**Priority:** P1 — positional native contexts and copied adapter mapping drift by product.  
**Source:** `libs/draxul-host/src/plugin_render_pass_vk.cpp`  
**Proposed by:** Claude 17, with PCBView migration narrowed. **Owner:** one SDK/host seam agent, then product owners. **Depends on:** card 21.  
**Evidence:** host Vulkan/Metal frame packing hardcodes scale/PPI despite `PluginHost::plugin_viewport()`; MegaCity/SatView reconstruct contexts; product adapters differ on focus/composition/unknown input.

**Boundary verification**
- [ ] Pin frame fields, prepass semantics, input coordinate/capture order and supported event kinds per product.
**Implementation and migration**
- [ ] Add typed frame/context mapping and `VkRenderContext` descriptor in allowlisted support; pass actual viewport scale/PPI. Add primitive input conversion, then migrate products without changing their semantic handling.
**Unit tests**
- [ ] GPU-free context/frame round trips and complete event mapping, including unknown kinds and local/global coordinates.
**Cross-platform validation**
- [ ] Preserve Vulkan no-active-pass and Metal nil-encoder prepass; run affected product aggregates/goldens and smoke.
**Agent documentation and tooling**
- [ ] Document SDK helper ownership and product migration order.
**Acceptance criteria**
- [ ] Migrated adapters share field mapping while retaining product-specific event effects.
