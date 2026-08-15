# External product plugin separation

## Product boundary

Draxul is the lean agentic harness: window and GPU submission, terminal and agent
runtimes, topology and plugin lifecycle, Kanban, and Kanban's Markdown preview.
SatView, MegaCity/BioView, and ScoreView are native plugins that own their source,
product-specific dependencies, assets, persistence, tests, and backend rendering.
The product repositories are expected to be mounted beneath `plugins/` as Git
submodules and built with Draxul, so they may consume explicitly public, generic
Draxul support libraries.

The dependency direction is one-way:

```text
plugin submodule -> public C SDK + whitelisted generic support libraries
Draxul executable -> plugin module through the versioned C ABI only
```

No product header, source, dependency, target, or semantic type may leak into
`app/`, `libs/`, or `modules/`. Generic support libraries must not include or
name a product. C++ support is a same-build implementation detail and should be
linked statically into the plugin; no Draxul C++ object, ownership contract, or
ABI crosses the DLL/dylib boundary. Removing or disabling every product
submodule must leave the core configure, build, tests, and smoke path working.

There is no compatibility requirement before release. The ABI may be replaced
instead of layered when that produces a smaller and cleaner contract.

## Allowed shared technology

Plugins may use narrow, generic support targets for logging and performance,
value and input types, HTTP, configuration primitives, fonts and text atlases,
ImGui input/rendering, and Vulkan/Metal resource helpers. Prefer leaf targets
such as `Draxul::PluginSupport::Render` over broad core targets such as the full
host or grid renderer.

Third-party libraries remain either common infrastructure when broadly useful
(for example SDL, GLM, ImGui, Vulkan/VMA, and stb) or plugin-owned when they are
specific to one product (for example SGP4, Verovio/audio dependencies, and
MegaCity's Tree-sitter/EnTT stack). A plugin may retain an independent
installed-SDK-only build, as ScoreView does, but that is a portability feature,
not the required boundary for an in-tree submodule.

## Vertical slices

1. **Installable SDK boundary.** Move the public C ABI to `sdk/`, export an
   installable CMake package, and build/load the triangle from a copied standalone
   source tree using only the installed SDK and plugin-owned dependencies.
2. **Raw GPU contract.** Replace build-matched rendering bridges with complete
   versioned Vulkan and Metal C structures. Exercise render, resize, visibility,
   input, services, and safe teardown through the standalone triangle.
3. **SatView.** Move the actual satellite data, simulation, UI, shaders, renderer,
   assets, persistence, and tests into its plugin. Remove every SatView dependency
   from the Draxul app and libraries.
4. **MegaCity/BioView.** Create one self-contained product plugin with explicit
   mode/configuration, moving code semantics, scene construction, shaders, UI,
   assets, and tests out of Draxul core.
5. **ScoreView.** Move notation, Verovio, transport, workers, audio, microphone,
   MIDI, UI, rendering, assets, and tests into the plugin. Delete the C++ Canvas2D
   and build-matched ImGui bridge rather than preserving private core objects.
6. **Core deletion.** Remove static product host kinds, providers, module targets,
   feature flags, app registrations, and product dependencies. Kanban and Markdown
   remain core functionality.
7. **Submodule integration and acceptance.** Replace product-specific root build
   wiring with generic plugin registration, narrow and whitelist shared support
   targets, prove each product is removable, and pass Windows and macOS package,
   integration, render, visibility, and smoke suites.

## Final-slice implementation plan

### 7A. Codify the dependency boundary

- Add a generic CMake plugin-registration API that accepts a module target,
  manifest, assets, shaders, runtime files, product test targets, and optional
  feature switch. Root CMake may enumerate mounted plugin directories, but must
  not know their internal targets, dependencies, shaders, or packaging layout.
- Define same-build static `Draxul::PluginSupport::*` targets and an explicit
  allowlist. Keep `Draxul::PluginSDK` as the only runtime ABI.
- Add a configure-time boundary check: core targets cannot link product targets;
  core source cannot include product headers; generic support cannot name or
  include SatView, MegaCity, BioView, or ScoreView types.

### 7B. Move build ownership into the submodules

- Move product dependency fetching out of `cmake/FetchDependencies.cmake`:
  SGP4 to SatView; Tree-sitter/its C++ grammar and EnTT to MegaCity; all Verovio,
  audio, MIDI, font, and notation dependencies to ScoreView.
- Move product sanitizer/coverage target lists, shader compilation, bundle
  staging, runtime-library copying, and focused test declarations into each
  plugin directory. Use generic Draxul helper functions rather than repeating
  platform staging logic.
- Keep root integration limited to discovering/enabling a submodule and attaching
  its generic stage target to the Draxul package.

### 7C. Narrow shared infrastructure

- Split the reusable plugin-facing pieces currently reached through
  `draxul-host`, `draxul-renderer`, `draxul-font`, `draxul-config`, `draxul-ui`,
  `draxul-gui`, `draxul-http`, and `draxul-vulkan-resources` into the smallest
  sensible leaf targets. Do not copy these implementations into every plugin.
- Provide C++ convenience wrappers around the raw C ABI only where they remain
  generic and same-build. Keep swapchain, window, pane, topology, terminal,
  grid-renderer, and application orchestration dependencies unavailable to
  plugins.
- Replace baked source-root lookup with the plugin directory and versioned path,
  storage, and UI-style services. A plugin may accept a source tree to visualize,
  but it must not assume that source tree is the Draxul checkout.

### 7D. Cut over each product

- SatView: use the generic runtime, render, text, configuration, HTTP, logging,
  and GPU-resource support targets; keep simulation, catalogues, UI, shaders,
  assets, SGP4, tests, and persistence in the SatView submodule.
- MegaCity/BioView: use the generic runtime, render, text, configuration,
  performance, tooltip/UI, and GPU-resource support targets; keep scanning,
  semantics, geometry, scene construction, Tree-sitter/EnTT, tests, and both
  visualization modes in the MegaCity submodule.
- ScoreView: retain the proven standalone build, but consume the same generic
  in-tree registration and packaging contract so root CMake no longer contains
  Verovio, audio, shader, test, or staging knowledge.

### 7E. Acceptance matrix

- Configure/build/smoke with no product submodules, each product alone, and all
  products together. Missing submodules must be a normal supported state.
- Inspect the link graph to prove `draxul`, app, server, client, host, renderer,
  Kanban, and Markdown targets have no product edge.
- Through an isolated server, use plugin list/get and pane/tab launch commands for
  every product; verify restart, resize, input, hidden-tab/Space scheduling,
  teardown, missing-plugin placeholders, and declarative layouts.
- Run focused product tests and deterministic render scenarios for SatView,
  MegaCity, BioView, and the Grieg ScoreView fixture on Windows/Vulkan and
  macOS/Metal.
- Retain ScoreView's copied-tree extraction smoke as an additional release check,
  not a requirement imposed on all submodule plugins.

## Slice gates

Every slice must leave the tree buildable and the normal Draxul smoke path usable.
Work stops after each slice so the installed artifact and attached UI behavior can
be checked before the next boundary is changed.
