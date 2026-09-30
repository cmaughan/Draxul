# Test native renderer operations through behavior seams

**Priority:** P1 — source-spelling assertions obstruct safe renderer changes.  
**Source:** `tests/nanovg_vulkan_sync_tests.cpp`  
**Proposed by:** Claude 19, 22 narrowed. **Owner:** one renderer agent, with platform review.  
**Evidence:** tests search renderer/NanoVG source for upload and frame statements; Vulkan already has `VkRendererOperations`; Metal capture has no equivalent seam, and both backends duplicate pitched channel conversion.

**Boundary verification**
- [ ] Map every statement check to the invariant it intends to protect; retain genuine CMake/shader-text checks.
**Implementation and migration**
- [ ] Extract capture/readback policy and pitched CPU conversion; add narrow native-operation recording where needed; replace only checks with equivalent behavior coverage.
**Unit tests**
- [ ] Fault, ordering, BGRA/pitch and NanoVG upload/retirement cases, including Metal completion-before-signal.
**Cross-platform validation**
- [ ] Run Windows Vulkan and macOS Metal renderer tests, shared snapshots, product scope where affected, and smoke.
**Agent documentation and tooling**
- [ ] Document native operation-seam expectations.
**Acceptance criteria**
- [ ] Tests survive implementation renames while protecting the same resource and synchronization behavior.
