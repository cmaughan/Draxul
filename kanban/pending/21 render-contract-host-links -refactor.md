# Separate neutral render contracts from native contexts and retarget hosts

**Priority:** P1 — neutral consumers inherit native GPU libraries and concrete renderer links.  
**Source:** `libs/draxul-plugin-support/CMakeLists.txt`  
**Proposed by:** Claude 15, 16. **Owner:** one render-contract/CMake agent.  
**Evidence:** `draxul-plugin-render-support` transitively links Vulkan/VMA or Metal; all leaves expose one include root. Markdown-host publicly and plugin-host privately link `draxul-renderer` while using base/native context APIs.

**Boundary verification**
- [ ] Inventory base versus native context headers and host include/link needs.
**Implementation and migration**
- [ ] Introduce neutral contract and typed native context leaves with scoped include roots; retain the support alias as the aggregate. Retarget Markdown/plugin hosts to contract, native context, SDL headers and ImGui core as used.
**Unit tests**
- [ ] Link-isolation and configure rejection for neutral consumers reaching `draxul-renderer` or native GPU libraries.
**Cross-platform validation**
- [ ] Preserve Vulkan/VMA and Metal ARC/framework links; run host aggregate, relevant renders and smoke.
**Agent documentation and tooling**
- [ ] Update module map and allowlist checks.
**Acceptance criteria**
- [ ] Neutral consumers see only neutral headers; native passes still encode on both GPUs.
