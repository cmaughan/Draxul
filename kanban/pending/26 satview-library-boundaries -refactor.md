# Split SatView into stable library boundaries

**Type:** refactor
**Priority:** 26
**Raised by:** GPT/Codex, Claude

## Goal

SatView's single target currently mixes catalogs, network/cache services, propagation, scene records, UI/host state, and both render backends. Split it without altering user behavior or cross-platform scene parity.

## Target shape

- `draxul-satview-core`: catalogs, coordinates, filters, ephemerides, propagation.
- `draxul-satview-services`: HTTP/cache refresh above item 00's transport.
- `draxul-satview-scene`: backend-neutral vertices, uniforms, draw records, snapshots.
- `draxul-satview-renderer`: private Vulkan/Metal implementations consuming one scene contract.
- `draxul-satview-host`: ImGui, input, camera, config, orchestration.

## Implementation plan

- [ ] Wait for current observatory/boundary/text-atlas work and item 18.
- [ ] Generate the current include/target dependency graph and classify every source file.
- [ ] Create targets in dependency order, starting with core and scene; use `PRIVATE` links by default.
- [ ] Move services after item 00 so shell transport is not preserved in the new boundary.
- [ ] Move backend selection behind the renderer target and keep public headers backend-neutral.
- [ ] Reduce duplicated Apple/non-Apple source lists with shared source sets plus backend additions.
- [ ] Consolidate SatView/MegaCity-style asset-path resolution and ImGui attachment only through a demonstrated lower-library contract; do not make the product modules depend on one another.
- [ ] Keep registration at the executable boundary and preserve `DRAXUL_ENABLE_SATVIEW=OFF` zero-coupling.
- [ ] Land each boundary as a buildable commit with no behavior changes.

## Tests and acceptance

- [ ] Assign focused tests to the narrowest target and keep the host smoke test.
- [ ] Configure/build with SatView ON and OFF on Windows and macOS paths.
- [ ] Vulkan and Metal consume identical scene records; no catalog/filter logic enters backends.
- [ ] Full build, focused SatView tests, `ctest`, render/startup check, and smoke pass.

## Dependencies and parallelism

Depends on 00/18 and active SatView work. After source classification, core/services and scene/renderer moves can be delegated, but one owner must control CMake and public boundaries.

<model>GPT-5 Codex</model>
