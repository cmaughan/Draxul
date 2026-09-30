# Centralize shader compilation and payload declarations

**Priority:** P2 — repeated GLSL/Metal command loops drift across products.  
**Source:** `cmake/CompileShaders_Metal.cmake`  
**Proposed by:** Claude 37. **Owner:** one CMake agent, then product owners.  
**Evidence:** core, spinning triangle and product CMake files repeat glslc/xcrun output and dependency construction.

**Boundary verification**
- [ ] Inventory flags, output names, include dependencies, staging and standalone product requirements.
**Implementation and migration**
- [ ] Add explicit CMake shader/payload helper; migrate core/reference plugin first, then products without renaming current targets.
**Unit tests**
- [ ] Configure fixtures and include-change rebuild checks.
**Cross-platform validation**
- [ ] Verify GLSL/Vulkan on Windows and MSL/Metal on macOS; run affected aggregate and smoke.
**Agent documentation and tooling**
- [ ] Ship or document helper for standalone product builds.
**Acceptance criteria**
- [ ] Shader changes rebuild the right output and stage the same payload on both platforms.
