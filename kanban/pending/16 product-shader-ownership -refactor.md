# Localize and gate product shaders

**Type:** refactor  
**Priority:** P1  
**Raised by:** Claude and Grok  
**Depends on:** pending `00`, `01`, `05`, `06`; ice-box `06`

## Boundary verification

- [ ] Record every shader entry point, include, owner, output filename, runtime lookup, and staging rule.
- [ ] Capture the current Vulkan and Metal output manifests.
- [ ] Identify genuinely shared headers.
- [ ] Verify product OFF configurations currently compile no product sources except the known shader leak.

## Implementation and migration

- [ ] Complete/reuse the explicit Metal dependency helper from ice-box `06`.
- [ ] Extend the helper to explicit cross-platform shader groups.
- [ ] Replace root Vulkan globs with explicit core/product groups.
- [ ] Gate Megacity and SatView groups.
- [ ] Move Markdown, SatView, and Megacity shaders to owning modules one at a time.
- [ ] Preserve runtime filenames and staging destinations.

## Unit/build tests

- [ ] Compare emitted output manifests before and after grouping.
- [ ] Build each shader group independently.
- [ ] Run shader ABI tests and relevant render snapshots.
- [ ] Configure all-optionals-off and one-product-enabled matrices.

## Cross-platform validation

- [ ] Compile/stage Vulkan SPIR-V on Windows.
- [ ] Compile/stage Metal libraries on macOS.
- [ ] Verify shared include changes rebuild only dependent groups.
- [ ] Confirm visual/pass-order parity.

## Agent documentation/tooling

- [ ] Update module-local shader ownership guidance.
- [ ] Add dependency-contract comments to shader-owning CMake files.

## Acceptance criteria

- [ ] Product OFF builds emit no product shaders.
- [ ] Routine product shader work does not edit root shader wiring.
- [ ] Output names and runtime lookup remain stable.
- [ ] Both backend builds and render suites pass.
