# Give the plugin ImGui Vulkan fork an owned API

**Priority:** P2 — products include an upstream header while linking a private fork.  
**Source:** `plugins/support/imgui/CMakeLists.txt`  
**Proposed by:** Claude 26, revised. **Owner:** one plugin support agent, then MegaCity/SatView owners.  
**Evidence:** plugin support compiles a modified Vulkan backend; renderer compiles upstream; product renderers call texture functions through `<backends/imgui_impl_vulkan.h>`.

**Boundary verification**
- [ ] Inventory fork extensions, texture callers, symbol ownership and Metal class-prefix behavior.
**Implementation and migration**
- [ ] Add plugin-owned texture registration/removal wrapper and private backend target/API; migrate product callers and then reject upstream backend includes in products. Keep GPU code out of `draxul-imgui-core`.
**Unit tests**
- [ ] Wrapper lifecycle and compile checks for both host and plugin backend presence.
**Cross-platform validation**
- [ ] Inspect Windows link map/build with both implementations; preserve per-plugin Metal Objective-C class renaming; run product renders and smoke.
**Agent documentation and tooling**
- [ ] Document backend API ownership.
**Acceptance criteria**
- [ ] Product code uses the plugin-owned API; no unverified ODR failure is assumed.
