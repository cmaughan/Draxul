# Decompose MegaCity host and renderer hotspots

**Type:** refactor
**Priority:** 27
**Raised by:** GPT/Codex, Claude

## Goal

Reduce `codeviz_render_vk.cpp`, `codeviz_render.mm`, and `megacity_host.cpp` by pass/resource/responsibility while preserving the existing Megacity sub-library boundaries and both backends.

## Implementation plan

- [ ] Follow `modules/megacity/AGENTS.md` and land shader ABI tests/item 19 first.
- [x] Inventory renderer clusters: device resources, attachments/GBuffer, shadows, AO, scene pass, postprocess, capture/debug, and uploads.
  - [x] Extract Vulkan mapped/persistent/transient buffers, meshes, sampled images, staging uploads, attachment/cube views, shader loads, explicit transitions, and destruction into private `codeviz_vk_resources.*`.
  - [x] Extract the demonstrated Metal buffer/mesh capacity and transient-arena family into private `codeviz_metal_resources.*`, preserving native `ObjCRef` ownership.
  - [ ] Keep the tightly coupled GBuffer/shadow/AO/scene/postprocess record order in the backend pass until focused pass-order and resize seams exist.
- [x] Extract private pass/resource-family helpers with explicit init/record/resize/shutdown contracts; keep Vulkan/Metal types private.
- [x] Share only backend-neutral pass inputs/scene records; do not force artificial identical backend classes.
- [x] Move inline picking math from `MegaCityHost::pump()` into the existing picking unit.
- [x] Separate scanner/build polling, camera/input, metrics overlays, scene publication, and ImGui panels into focused host collaborators.
  - [x] Centralize immutable scene snapshot publication for both `pump()` and `draw()`.
  - [x] Centralize selection identity/remap queries shared by picking and opacity updates.
  - [x] Extract scanner/build polling, camera/input, metrics overlays, and ImGui panels into `SemanticSourceController`, `MegacityCameraInput`, `MetricsOverlayController`, and `MegacityHostPanelFrame`.
- [x] Preserve immutable snapshot handoff, cancellation/generation checks, and main-thread GPU ownership in the completed host extractions.
- [x] Split the 3,400-line `megacity_scene_tests.cpp` by stable host/input, semantic-layout/routing, and scene-world/presentation responsibilities while preserving the existing Catch2 cases and tags.
- [x] Land one pass/host responsibility at a time and avoid concurrent edits to the same giant file.

## Tests and acceptance

- [ ] Add/extend focused scene, picking, pass-order, resize, and lifecycle tests before each move.
- [ ] Build with MegaCity ON/OFF and inspect both Vulkan and Metal implementations.
- [ ] Launch `--host megacity` on the available platform and state other-platform runtime status.
- [ ] No user-visible render/config change; full tests/render checks/smoke pass.

## Dependencies and parallelism

Depends on item 19; item 28 may provide helpers first. Sub-agents make sense by non-overlapping pass family after interfaces are fixed.

## Renderer validation notes

- Windows Debug compile-only validation passed for `draxul-codeviz-renderer` after the Vulkan extraction; the target compiled both `codeviz_render_vk.cpp` and `codeviz_vk_resources.cpp`.
- The Metal extraction mirrors only the demonstrated native buffer/arena boundary and was inspected on Windows; macOS compilation/runtime validation remains outstanding.
- No helper, test, or application executable was launched during this extraction.

## Host validation notes

- Windows Release compile-only validation passed for `draxul-megacity` after extracting scanner/semantic publication, camera/input, metrics-overlay, and ImGui frame/input collaborators.
- The host source dropped from roughly 2,800 to roughly 2,400 lines in this step; immutable scene publication and worker cancellation/generation behavior remain at their prior boundaries.
- `megacity_scene_tests.cpp` is already split into focused host/input, layout/routing, and world/presentation translation units. New focused selection, publication, semantic-source lifecycle, and shutdown-order coverage was added; the Release `draxul-test-megacity` target compiled and linked successfully without launching the test binary.

<model>GPT-5 Codex</model>
