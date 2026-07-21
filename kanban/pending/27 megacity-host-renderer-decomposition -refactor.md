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

- Windows Release with MegaCity enabled passed the full 22-entry CTest suite,
  including both MegaCity shards, app smoke, and all render snapshots.
- The warm Release build was reconfigured with `DRAXUL_ENABLE_MEGACITY=OFF`;
  `draxul` and `draxul-tests` built and all 14 available unit entries passed.
  The normal tree was then restored to `DRAXUL_ENABLE_MEGACITY=ON`, rebuilt, and
  both MegaCity shards passed again.
- Windows Debug compiled the Vulkan renderer/resource split and passed both
  MegaCity shards, app smoke, and all five Vulkan render snapshots under the
  validation-layer path. The only Debug suite failure is the session pipe
  security assertion recorded on item 25, outside MegaCity.
- The Metal extraction mirrors only the demonstrated native buffer/arena boundary and was inspected on Windows; macOS compilation/runtime validation remains outstanding.
- The user launched and visually checked PC behavior with no render or behavior
  regression observed. Metal build/runtime and explicit macOS visual status are
  still outstanding, so the cross-backend acceptance boxes remain open.

## Host validation notes

- Windows Release and Debug automated MegaCity suites pass after extracting
  scanner/semantic publication, camera/input, metrics-overlay, and ImGui
  frame/input collaborators.
- The host source dropped from roughly 2,800 to roughly 2,400 lines in this step; immutable scene publication and worker cancellation/generation behavior remain at their prior boundaries.
- `megacity_scene_tests.cpp` is already split into focused host/input,
  layout/routing, and world/presentation translation units. New focused
  selection, publication, semantic-source lifecycle, and shutdown-order
  coverage was added; both MegaCity CTest shards now pass in Windows Release and
  Debug.

<model>GPT-5 Codex</model>
