# External product plugin separation

## Product boundary

Draxul is the lean agentic harness: window and GPU submission, terminal and agent
runtimes, topology and plugin lifecycle, Kanban, and Kanban's Markdown preview.
SatView, MegaCity/BioView, and ScoreView are native plugins that own their source,
dependencies, assets, persistence, tests, and backend rendering. A product plugin
must build without access to Draxul's `app/`, `libs/`, or `modules/` trees.

There is no compatibility requirement before release. The ABI may be replaced
instead of layered when that produces a smaller and cleaner contract.

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
7. **Extraction acceptance.** Build each copied plugin against an installed SDK,
   stage it beside a clean Draxul package, drive it through pane/tab commands, and
   pass Windows and macOS build, integration, render, visibility, and smoke suites.

## Slice gates

Every slice must leave the tree buildable and the normal Draxul smoke path usable.
Work stops after each slice so the installed artifact and attached UI behavior can
be checked before the next boundary is changed.
