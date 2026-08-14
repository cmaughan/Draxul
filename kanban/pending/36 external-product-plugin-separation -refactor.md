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
- [ ] Slice 7: finish generic submodule integration, packaging, UI behavior, and macOS parity.
  - [x] 7A: generic plugin registration, shared-support allowlist, and dependency guards.
  - [x] 7B: move product dependencies, tests, shaders, and staging into each submodule.
  - [x] 7C: narrow broad core dependencies into generic plugin-support leaf targets.
  - [x] 7D: cut SatView, MegaCity/BioView, and ScoreView over to that contract.
  - [ ] 7E: prove no-plugin/one-plugin/all-plugin builds plus Windows/macOS runtime acceptance.

Slice 7 progress: ScoreView now has an opt-in copied-tree extraction smoke that
builds against only the installed SDK and plugin-owned support, then loads the
result in a clean Draxul package. That remains a useful extra portability check,
but it is no longer the required model for every product. SatView and MegaCity
may consume whitelisted generic same-build support libraries while mounted as
submodules; product code and dependencies must remain inside the submodule, core
must have no reverse dependency, and the runtime boundary remains C-only.

Slice 7A adds `cmake/DraxulPlugins.cmake` as the generic mounted-submodule
contract. The triangle now registers and stages its own manifest, module, and
platform shader payload through that API. Configure-time checks reject reverse
core-to-product links, product-header includes from core, and registered plugins
that link Draxul targets outside the explicit `Draxul::PluginSupport::*`
allowlist. The installed-SDK triangle smoke built an isolated DLL, loaded it in a
clean Draxul package, and rendered a non-blank 960x640 frame on Windows.

Slice 7B moves SGP4, Tree-sitter/EnTT, and the ScoreView notation/audio
dependency stack out of root FetchDependencies and into their products. Each
plugin now registers its own shaders, assets, runtime libraries, build policy,
test inventory, and focused test CMake; SatView's Python catalog test and all
catalog/texture generators moved with SatView. Root CMake now knows only the
feature switches and plugin mount directories. The unchanged 204-source/2072-
registration/307-tag inventory built successfully; all product shards passed,
and real staged SatView, MegaCity, BioView, and Grieg ScoreView render captures
were exported on Windows. Product support links remain explicitly marked
`MIGRATING` until Slice 7D replaces their broad core dependencies.

Slice 7C introduces the physical `libs/draxul-plugin-support` boundary. Generic
render contracts and typed Vulkan/Metal contexts moved out of the renderer;
Vulkan resource ownership, explicit-path TOML documents, and tooltip rasterizing
became independent leaves. Existing text, HTTP, type, performance, and ImGui
libraries now have explicit `Draxul::PluginSupport::*` names. A product-neutral
C++ wrapper copies the versioned path, storage, and UI-style service tables
without owning any Draxul object, and a real dynamically loaded fixture proved
those wrappers across the DLL boundary. Configure now rejects any allowlisted
leaf that links back into broad core. Products remain marked `MIGRATING` for the
7D source and link cutover.

Slice 7D removes that migration exemption. SatView and MegaCity/BioView now use
the generic plugin runtime/viewport and render contracts rather than
`draxul-host`, and every product target links only its own targets, third-party
libraries, or named `Draxul::PluginSupport::*` leaves. ScoreView uses the same
strict in-tree support name while retaining its standalone SDK build. Packaged
asset roots, source scan roots, LCOV lookup, and product UI state are resolved
from explicit plugin configuration/services rather than compile-time Draxul
source paths. All three registrations now pass the strict configure-time graph
check; focused Windows product builds and test shards pass. Cross-platform and
feature-matrix runtime proof remains 7E.

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
