# Share the plugin-frame NanoVG adapter

**Priority:** P2 — removes duplicated lifecycle and frame conversion across two product repositories.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 4.  
**Owner:** One core support owner, followed by separate PCBView and ScoreView adoption owners.  
**Dependencies:** `kanban/pending/00 internal-target-build-policy -refactor.md`. Independent of PCB selection and paint conversion after interface/CMake ownership is assigned.

**Evidence:** PCBView’s `src/nanovg_pass_vk.cpp:19–117` duplicates ScoreView’s `product/draxul-score-canvas/src/nanovg_pass_vk.cpp:18–117`; corresponding Metal files duplicate frame/callback handling. Existing canvas targets own these wrappers. `kanban/done/38 shared-library-consolidation -refactor.md` consolidated only the underlying backend.

**Boundary:** Add `draxul-plugin-nanovg-support STATIC`, exposed as `Draxul::PluginSupport::NanoVGPass`. A uniquely named header/namespace accepts existing SDK frame structures and drawing callbacks, with factory and shader-root setup. Native handles remain private. Depend on SDK, NanoVG backend, and native resource providers; preserve the separate core `IRenderPass` adapter.

#### Boundary verification

- [ ] Compare both products’ callback retention, viewport conversion, context lifecycle, and shader-root behavior.
- [ ] Freeze a header name that cannot collide with core `draxul/nanovg_pass.h`.
- [ ] Define bundled and standalone resource-provider wiring before product migration.
- [ ] Coordinate allocation-failure coverage with `kanban/pending/16 metal-nanovg-failure-ownership -bug.md`.

#### Implementation and migration

- [ ] Add the support leaf and register its alias in the root support allowlist.
- [ ] Migrate PCBView first, preserving product drawing ownership.
- [ ] Migrate ScoreView and copy/stage the new support directory in `external_product_plugin_smoke.py`.
- [ ] Preserve standalone `draxul-scoreview-vma-impl`; do not require unavailable bundled Vulkan resources.
- [ ] Remove duplicated native adapters after both consumers pass.

#### Unit tests

- [ ] Add `draxul-test-plugin-nanovg` using private recording operations for viewport/scissor/translation, frame forwarding, callback consumption, initialization failure/retry, and destruction order.
- [ ] Characterize Vulkan device/format reset separately from existing Metal initialization behavior.
- [ ] Enable `cmake --build <cache> --config Debug --target draxul-test-plugin-nanovg --parallel`.
- [ ] Enable `ctest --test-dir <cache> -C Debug -R '^draxul-test-plugin-nanovg-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [ ] Preserve PIC, module symbol isolation, NanoVG shader staging, and Metal ARC.
- [ ] Preserve bundled macOS SDL header-only linkage versus standalone/Windows SDL linkage.
- [ ] Run `cmake --build <cache> --config Debug --target draxul-scoreview-extraction-smoke --parallel`; this is a custom target, not a CTest entry.
- [ ] Run `python3 do.py test debug --products`, then `python3 do.py smoke debug --skip-build`.
- [ ] Verify PCBView and ScoreView native rendering on Vulkan and Metal.

#### Agent documentation/tooling

- [ ] Register focused tests in core aggregates and runner selection.
- [ ] Document support dependencies and standalone staging in core/product guides.
- [ ] Land core support first, then product commits and deliberate pointer adoption.

#### Acceptance criteria

- [ ] Both products use one adapter implementation per backend.
- [ ] Support has no app, renderer, window, or product dependency.
- [ ] ScoreView’s copied-tree build and render smoke remain valid.
- [ ] Existing platform behavior is preserved without introducing unrelated parity changes.
