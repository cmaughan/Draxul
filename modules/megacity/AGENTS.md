# Megacity Module Agent Guide

## Scope

This guide applies to everything under `modules/megacity/` and supplements the repository-root `AGENTS.md`. Follow both; this file contains the Megacity-specific constraints.

Megacity is the optional 3D semantic code-city host. Treat it as a product module rather than a renderer demo. It must remain removable with `DRAXUL_ENABLE_MEGACITY=OFF`: core libraries and the terminal product must not acquire unguarded source, header, link, test, shader, or asset dependencies on this directory.

Do not use the MaaS MCP tools/servers for work in this module.

## Module Structure

- `draxul-geometry`: Renderer-independent procedural mesh generation and shared `GeometryMesh` data. Keep it deterministic, GLM-based, and free of Vulkan, Metal, host, and ImGui dependencies.
- `draxul-treesitter`: Background source scanning and raw parsed-symbol snapshots. Keep filesystem/parsing concerns here and publish immutable snapshots to consumers.
- `draxul-citymodel`: Projects parsed source data into semantic city records and resolves repository module paths. It may depend on `draxul-treesitter`, but must not depend on the host or GPU renderer.
- `draxul-megacity`: Host lifecycle, input, configuration, semantic layout, scene snapshots, ImGui panels, and platform render passes. Push pure geometry, parsing, and semantic-model logic into the lower libraries above.

Keep the dependency direction approximately:

```text
draxul-geometry -----------------------> draxul-megacity
draxul-treesitter -> draxul-citymodel -> draxul-megacity
```

Avoid publishing Megacity-specific types outside this module unless another product module has a demonstrated need for them. Public API headers belong under the owning library's `include/draxul/`; internal headers belong under `src/`. Never duplicate a header in both places.

## Rendering

- Windows rendering lives in `draxul-megacity/src/megacity_render_vk.cpp`; macOS rendering lives in `draxul-megacity/src/megacity_render.mm`.
- Shared CPU scene and snapshot definitions must stay backend-neutral. Do not expose Vulkan or Metal types through public headers or shared scene records.
- A rendering feature is incomplete until both backends have been inspected and kept behaviorally aligned. When changing vertex formats, uniforms, materials, attachments, passes, resource lifetimes, debug views, or bindings, update the Vulkan/GLSL and Metal/MSL paths together or explicitly report the remaining platform gap.
- Megacity shader sources live in the repository-root `shaders/` directory, and their compilation/copy wiring lives in the top-level CMake files. Keep shader structs, binding indices, formats, and color-space assumptions synchronized with the C++/Objective-C++ code.
- Material assets live under `assets/megacity/`. New runtime assets need install/copy wiring and a useful failure log when missing.
- Keep GPU and ImGui state on the main thread. Worker threads may build CPU-only immutable results, but must not create, mutate, or destroy renderer resources.

## Semantic Model And Layout

- Preserve the boundary between semantic model construction and spatial layout. Semantic records describe code; layout turns those records into positions, lots, roads, routes, and renderable scene data.
- Use stable semantic identity, including module path and source path where needed. Class or function names alone are not unique.
- Keep pure layout and mesh generation deterministic so they remain easy to test without a window or GPU.
- `GeometryMesh` currently uses 16-bit indices. Generators must stay within that index range or deliberately change the shared format and both render backends together.
- Prefer immutable snapshots for scene handoff. Replacing a complete snapshot is safer than exposing partially rebuilt vectors across host, worker, and render code.

## Threading And Lifetime

Megacity has background work in the Tree-sitter scanner, city-grid builds, and dependency-route builds. For all of them:

- Use cancellation plus generation checks so stale work cannot replace newer state.
- Publish completed data under the appropriate lock or through immutable shared ownership.
- Do not perform an unbounded worker join from `pump()`, `draw()`, or an input callback.
- Make shutdown deterministic: signal cancellation, wake waiting workers, join them, and only then release state they can access.
- Capture configuration and model inputs by value or shared immutable ownership when a worker outlives the initiating call.

## Build Wiring

- Shared Megacity sources must appear in both platform source lists in `draxul-megacity/CMakeLists.txt`; backend-only files stay in the corresponding branch.
- Keep all Megacity CMake additions behind `DRAXUL_ENABLE_MEGACITY` at repository integration points.
- After build-wiring changes, configure and build once with Megacity enabled and verify that configuration/build still succeeds with `-DDRAXUL_ENABLE_MEGACITY=OFF`.
- Host registration belongs at the optional-module boundary through `register_megacity_host_provider()`; do not teach core host libraries to construct `MegaCityHost` directly.

## Tests And Validation

During development, run the narrow Catch2 tags that match the changed layer:

```powershell
cmake --build build --config Release --target draxul-tests
& .\build\Release\draxul-tests.exe "[megacity],[geometry],[treesitter],[lcov]"
```

On macOS, the equivalent test binary is `./build/draxul-tests`.

Before completing Megacity work, follow the root validation rules: build `draxul` and `draxul-tests`, run the smoke test, and run `ctest` when renderer, host lifecycle, configuration, Tree-sitter, or semantic behavior changed. For rendering changes, launch `--host megacity` on the affected platform and inspect startup and the changed view. If only one platform is available, inspect the other backend carefully and state that its runtime validation remains outstanding.

Add or update tests in the existing focused files under `tests/`, especially:

- `megacity_scene_tests.cpp` for layout, routing, input, host lifecycle, snapshots, and configuration behavior
- `megacity_static_mesh_family_tests.cpp` and `draxul_geometry_tests.cpp` for mesh generation and reuse
- `treesitter_tests.cpp` and `treesitter_semantic_source_tests.cpp` for scanning and semantic projection
- `lcov_coverage_tests.cpp` for coverage import and matching

Do not bless unrelated render snapshots to hide a regression.

## Documentation

Update `docs/features.md` for user-visible Megacity behavior, controls, configuration, CLI, assets, or build options. Keep detailed architectural explanations in `docs/architecture/` and reusable investigation results in `docs/learnings/` rather than expanding this guide with implementation history.
