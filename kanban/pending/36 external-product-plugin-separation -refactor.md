# External product plugin separation

The earlier plugin cutover changed launch routing, but it did not produce a clean
product boundary: SatView still links Draxul modules, MegaCity is still a core
host, and ScoreView crosses a build-matched C++ canvas bridge. Complete the clean
separation in
[`plans/external-product-plugin-separation.md`](../../plans/external-product-plugin-separation.md).

- [x] Slice 1: installable public SDK and isolated triangle consumer proof.
- [x] Slice 2: raw Vulkan/Metal ABI proven through the triangle.
- [x] Slice 3: self-contained SatView plugin with its real renderer and runtime.
- [x] Slice 4: self-contained MegaCity plugin including BioView mode.
- [x] Slice 5: self-contained ScoreView plugin with no Draxul C++ bridge.
- [x] Slice 6: remove the old product host/module layer; retain Kanban and Markdown.
- [ ] Slice 7: prove repository extraction, packaging, UI behavior, and macOS parity.

Slice 5 correction: the public `draxul.ui-style` service now carries Draxul's
recommended font, scale, and change generation. ScoreView consumes it while
retaining its own Vulkan/Metal ImGui renderer, restoring the original inspector
scale without reintroducing a C++ renderer bridge.

Slice 6 removed the `MegaCity`/`BioView` core host kinds and factories, the
private Canvas2D/ImGui host services, app registration, and app-test product
linkage. SatView, MegaCity, and ScoreView now share product-owned raw-GPU ImGui
support linked into their modules. Windows Release validation passed all nine
focused app/product shards, all four product render scenarios, and the direct
application smoke test. macOS runtime validation remains part of Slice 7.

Each slice stops at a user-visible acceptance gate. Prefer isolated builds,
server/UI integration, render snapshots, and smoke tests over low-value unit tests.
