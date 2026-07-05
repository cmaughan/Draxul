# Decompose MegaCity host and renderer hotspots

**Type:** refactor
**Priority:** 27
**Raised by:** GPT/Codex, Claude

## Goal

Reduce `codeviz_render_vk.cpp`, `codeviz_render.mm`, and `megacity_host.cpp` by pass/resource/responsibility while preserving the existing Megacity sub-library boundaries and both backends.

## Implementation plan

- [ ] Follow `modules/megacity/AGENTS.md` and land shader ABI tests/item 19 first.
- [ ] Inventory renderer clusters: device resources, attachments/GBuffer, shadows, AO, scene pass, postprocess, capture/debug, and uploads.
- [ ] Extract private pass/resource-family helpers with explicit init/record/resize/shutdown contracts; keep Vulkan/Metal types private.
- [ ] Share only backend-neutral pass inputs/scene records; do not force artificial identical backend classes.
- [ ] Move inline picking math from `MegaCityHost::pump()` into the existing picking unit.
- [ ] Separate scanner/build polling, camera/input, metrics overlays, scene publication, and ImGui panels into focused host collaborators.
- [ ] Preserve immutable snapshot handoff, cancellation/generation checks, and main-thread GPU ownership.
- [ ] Split the 3,400-line `megacity_scene_tests.cpp` by the same stable responsibility boundaries as production code while preserving Catch2 tags/case counts.
- [ ] Land one pass/host responsibility at a time and avoid concurrent edits to the same giant file.

## Tests and acceptance

- [ ] Add/extend focused scene, picking, pass-order, resize, and lifecycle tests before each move.
- [ ] Build with MegaCity ON/OFF and inspect both Vulkan and Metal implementations.
- [ ] Launch `--host megacity` on the available platform and state other-platform runtime status.
- [ ] No user-visible render/config change; full tests/render checks/smoke pass.

## Dependencies and parallelism

Depends on item 19; item 28 may provide helpers first. Sub-agents make sense by non-overlapping pass family after interfaces are fixed.

<model>GPT-5 Codex</model>
