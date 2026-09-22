# Share the plugin-frame NanoVG adapter

**Priority:** P2 — removes duplicated lifecycle and frame conversion across two product repositories.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 4.  
**Owner:** One core support owner, followed by separate PCBView and ScoreView adoption owners.  
**Dependencies:** `kanban/pending/00 internal-target-build-policy -refactor.md`. Independent of PCB selection and paint conversion after interface/CMake ownership is assigned.

**Evidence:** PCBView’s `src/nanovg_pass_vk.cpp:19–117` duplicates ScoreView’s `product/draxul-score-canvas/src/nanovg_pass_vk.cpp:18–117`; corresponding Metal files duplicate frame/callback handling. Existing canvas targets own these wrappers. `kanban/done/38 shared-library-consolidation -refactor.md` consolidated only the underlying backend.

**Boundary:** Add `draxul-plugin-nanovg-support STATIC`, exposed as `Draxul::PluginSupport::NanoVGPass`. A uniquely named header/namespace accepts existing SDK frame structures and drawing callbacks, with factory and shader-root setup. Native handles remain private. Depend on SDK, NanoVG backend, and native resource providers; preserve the separate core `IRenderPass` adapter.

#### Boundary verification

- [x] Compare both products’ callback retention, viewport conversion, context lifecycle, and shader-root behavior.
- [x] Freeze a header name that cannot collide with core `draxul/nanovg_pass.h`.
- [x] Define bundled and standalone resource-provider wiring before product migration.
- [x] Coordinate allocation-failure coverage with `kanban/pending/16 metal-nanovg-failure-ownership -bug.md`.

#### Implementation and migration

- [x] Add the support leaf and register its alias in the root support allowlist.
- [x] Migrate PCBView first, preserving product drawing ownership.
- [x] Migrate ScoreView and copy/stage the new support directory in `external_product_plugin_smoke.py`.
- [x] Preserve standalone `draxul-scoreview-vma-impl`; do not require unavailable bundled Vulkan resources.
- [x] Remove duplicated native adapters after both consumers pass.

#### Unit tests

- [x] Add `draxul-test-plugin-nanovg` using private recording operations for viewport/scissor/translation, frame forwarding, callback consumption, initialization failure/retry, and destruction order.
- [x] Characterize Vulkan device/format reset separately from existing Metal initialization behavior.
- [x] Enable `cmake --build <cache> --config Debug --target draxul-test-plugin-nanovg --parallel`.
- [x] Enable `ctest --test-dir <cache> -C Debug -R '^draxul-test-plugin-nanovg-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [x] Preserve PIC, module symbol isolation, NanoVG shader staging, and Metal ARC.
- [x] Preserve bundled macOS SDL header-only linkage versus standalone/Windows SDL linkage.
- [x] Run `cmake --build <cache> --config Debug --target draxul-scoreview-extraction-smoke --parallel`; this is a custom target, not a CTest entry.
- [x] Run `python3 do.py test debug --products`, then `python3 do.py smoke debug --skip-build`.
- [x] Verify the shared adapter, PCBView, and ScoreView native rendering on macOS/Metal.
- [x] Verify PCBView and ScoreView native rendering on Windows/Vulkan.

#### Agent documentation/tooling

- [x] Register focused tests in core aggregates and runner selection.
- [x] Document support dependencies and standalone staging in core/product guides.
- [x] Keep product adoption in product commits and deliberately pin both product revisions from the root repository.

#### Acceptance criteria

- [x] Both products use one adapter implementation per backend.
- [x] Support has no app, renderer, window, or product dependency.
- [x] ScoreView’s copied-tree build and render smoke remain valid.
- [x] Existing platform behavior is preserved without introducing unrelated parity changes.

## Validation

- macOS focused adapter and paint targets: 4/4 CTest shards passed.
- macOS copied-tree `draxul-scoreview-extraction-smoke`: standalone build, module load, and extracted ScoreView Metal render passed.
- macOS `python3 do.py test debug --products`: 39/39 core and product CTest entries passed.
- macOS shared NanoVG render comparison: exact match, 0 changed pixels.
- macOS native plugin render exports: PCBView produced a 960x700 32-bit frame with 16,203 colors; bundled ScoreView produced a 960x640 32-bit frame with 3,724 colors.
- macOS `python3 do.py smoke debug --skip-build`: passed with Metal on Apple M5.
- Windows/Vulkan source paths and resource-provider wiring remain intact, but no Windows runtime was available for native render validation.

## Windows closeout (2026-09-22)

PCBView rendered through the products gate. ScoreView was visually reviewed,
its deterministic one-pixel scrollbar-thumb baseline drift was refreshed, and
the rerun passed; the 48/48 aggregate and same-cache smoke also passed.
