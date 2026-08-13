# External product plugin separation

The earlier plugin cutover changed launch routing, but it did not produce a clean
product boundary: SatView still links Draxul modules, MegaCity is still a core
host, and ScoreView crosses a build-matched C++ canvas bridge. Complete the clean
separation in
[`plans/external-product-plugin-separation.md`](../../plans/external-product-plugin-separation.md).

- [x] Slice 1: installable public SDK and isolated triangle consumer proof.
- [x] Slice 2: raw Vulkan/Metal ABI proven through the triangle.
- [x] Slice 3: self-contained SatView plugin with its real renderer and runtime.
- [ ] Slice 4: self-contained MegaCity plugin including BioView mode.
- [ ] Slice 5: self-contained ScoreView plugin with no Draxul C++ bridge.
- [ ] Slice 6: remove the old product host/module layer; retain Kanban and Markdown.
- [ ] Slice 7: prove repository extraction, packaging, UI behavior, and macOS parity.

Each slice stops at a user-visible acceptance gate. Prefer isolated builds,
server/UI integration, render snapshots, and smoke tests over low-value unit tests.
