# Module Map

This page is the human-friendly entry point for understanding how Draxul is put together.

Use it when you want to answer:

- where a change probably belongs
- how the main libraries relate to each other
- which generated diagrams to look at first
- which validation flows are most relevant after a change

## Start Here

If you only want the shortest path to orientation:

1. Read the top-level layout in [README.md](../README.md).
2. Read the maintained high-level dependency shape below, then use the generated
   [deps.svg](deps/deps.svg) for exact edges from its last documentation build.
3. Look at the class diagram in [draxul_classes.svg](uml/draxul_classes.svg).
4. Generate local API docs with `python scripts/build_docs.py --api-only`, then open `docs/api/index.html`.
5. Check active follow-up items in [kanban/pending/](../kanban/pending/).
6. Use the snapshot and smoke flows through [do.py](../do.py) when touching UI or rendering.

## Dependency Shape

`CMakeLists.txt` files are authoritative. At a useful orientation level, the current
target graph has four layers:

```text
draxul executable
├── draxul-app
├── draxul-client / draxul-server
├── draxul-markdown-host
├── draxul-kanban
└── optional products: draxul-megacity / SatView + ScoreView plugins
        │
        ├── product-specific model, scene, service, and renderer targets
        └── shared host / renderer / UI infrastructure
                │
                ├── draxul-host (including RemoteTerminalHost) /
                │   draxul-runtime-support / draxul-render-test
                ├── draxul-config / draxul-gui / draxul-ui / draxul-nanovg
                └── draxul-http / draxul-font / draxul-grid / draxul-nvim /
                    draxul-renderer / draxul-window / draxul-imgui-core
                            │
                            └── narrow foundations: draxul-types / draxul-performance /
                                draxul-agent / draxul-session-model / draxul-bmp / draxul-host-identity /
                                draxul-terminal-core / draxul-terminal-process
```

This is a dependency shape, not a promise that every target in one row links every
target below it. Consult the target's `CMakeLists.txt` for exact public/private edges.
In particular, `draxul-app` deliberately has no source-level dependency on the
SatView or ScoreView product runtimes; the executable discovers their modules.

## Core Libraries

### app/

Top-level orchestration only.

Owns:
- process startup/shutdown
- wiring between window, renderer, text service, grid, and Neovim RPC
- the `ITopologyMutationRoute` adapter boundary: local controller mutations and
  server-backed command translation share one App-facing operation surface
- smoke/render-test entry points

Good place for:
- app lifecycle
- top-level CLI/test harness behavior
- integration glue

Bad place for:
- backend-specific renderer logic
- low-level font logic
- grid mutation rules

### libs/draxul-types/

Shared low-level data types and cross-module contracts.

Owns:
- shared structs
- event types
- highlight/logging/support types
- canonical small helpers shared across the tree: `string_util.h`
  (`trim`/`trim_view`/`ascii_lower`/`describe_text_for_log`), `input_types.h`
  modifier normalization (`normalize_modifiers`/`has_only_modifiers`, lock-key
  aware), `process_util.h` (Windows arg quoting, `spawn_detached`,
  `resolve_exec_paths`, `build_child_environment`, `current_unix_time_ms`,
  `process_start_token`), and `runtime_path.h` user directories
  (`user_config_dir`/`user_data_dir`/`user_cache_dir`)

Good place for:
- narrow shared contracts
- POD-like data passed between modules

`draxul-types` (headers + static lib) also ships with the `draxul-plugin-sdk`
install component so out-of-tree product builds can link it.

Compiled BMP I/O, runtime performance collection, and host/provider identity are
deliberately not owned here. They live in the narrowly named `draxul-bmp`,
`draxul-performance`, and `draxul-host-identity` targets so changing those facilities
does not rebuild or extend the universal value-type archive.

### Foundation support targets

| Directory | Ownership |
|---|---|
| `sdk/` | Installable, versioned native plugin C ABI and the dependency-free `Draxul::PluginSDK` CMake target |
| `plugins/support/imgui/` | Product-owned optional ImGui Vulkan/Metal encoder, the shared `PluginImGuiContext` lifecycle and `ImGuiInputBridge` event routing, and public UI-style service client, linked inside native plugin modules and never exposed across the ABI |
| `libs/draxul-imgui-core/` | The single SDL-scancode-to-ImGuiKey table and `IImGuiHost` backend interface; leaf-narrow (links only ImGui + SDL headers) so core UI, the renderer, and plugin support all consume the same definitions (`Draxul::PluginSupport::ImGuiCore`) |
| `libs/draxul-plugin-support/` | Same-build plugin leaves: C-ABI path/storage wrappers, the plugin adapter shell (`Draxul::PluginSupport::Adapter` — result factories, config parse, pane-state persistence, action registrar, `kApi` assembly; its header-only core `draxul/plugin_adapter.h` ships with the SDK install component), product lifecycle/viewport vocabulary, backend-neutral render contracts, Vulkan resource ownership incl. the shared adapter VMA allocator, the HDR/MSAA scene scaffolding (`Draxul::PluginSupport::VulkanResources` — attachments, the per-format MSAA probe, shader loading, immediate image upload, and `HdrScenePipeline`, which owns the one set of scene/tone-map subpass dependency masks both 3D products use), the camera key-latch and drag-inertia layer (`Draxul::PluginSupport::CameraInput`), explicit-path TOML documents, and tooltip layout; exports only `Draxul::PluginSupport::*` targets |
| `libs/draxul-performance/` | Runtime timing collection and the `PERF_MEASURE` instrumentation API |
| `libs/draxul-bmp/` | RGBA frame BMP read/write only; depends on frame value types and performance support |
| `libs/draxul-host-identity/` | Neutral `HostKind` identity/parsing contract shared by host and runtime APIs |
| `libs/draxul-plugin/` | Manifest discovery, platform dynamic loading, ABI/identity validation, and process-lifetime module cache; consumes the public SDK but does not own it |
| `libs/draxul-agent/` | Neutral agent identity/profile/runtime values plus bundled, versioned terminal-status and process-discovery evaluators |
| `libs/draxul-session-model/` | Renderer-free durable Session/Space/tab/pane snapshot values, validation, versioned TOML codec, and transactional file replacement shared by the app and server |
| `libs/draxul-control/` | Versioned, authenticated local Session control transport and client. Its public facade owns request dispatch and compatibility error mapping; private common sources own framing, absolute deadlines, and metadata caching, while CMake selects exactly one current-user-only backend (Windows named pipes or POSIX Unix-domain sockets). Backend handles and native error types do not escape the library; white-box access is limited to the test-only `draxul-control-test-internals` target. |
| `libs/draxul-protocol/` | Renderer- and transport-neutral server hello/status, versioned topology and sanitized agent-projection values, terminal pane/snapshot/delta/controller/clipboard/scrollback values, and the bounded `session.poll` request/response contract; also the single topology→session layout conversion (`topology_tab_to_layout` in `topology_layout.h`, agent identity preserved), the shared topology traversal (`find_space`/`find_tab`/`find_pane`/`find_node`), `topology_id_serial`, and the split-ratio bounds constants used by both the server (reject) and app (clamp) sides |
| `libs/draxul-client/` | Singleton discovery/launch/status/shutdown plus renderer-free remote-terminal and sanitized-agent delivery, and `TopologyProjection`, which owns remote/local Space-tab-pane identities, stable leaf/split projection, structural signatures, divider-node mapping, and topology command activation bookkeeping behind the app's thin controller/PaneManager adapters. `RemoteSessionCoordinator` owns terminal registrations, commands, recovery, visibility generations, projection mailboxes, and coalesced UI wakes; negotiated `session-poll-v1` uses one recurring batch worker per UI and externally feeds topology/agents, while older servers retain the Phase-1 per-channel workers. The library also owns the scrollback-page client, attach-latency metric, headless probe API, `ServerControlChannel` (the one control-plane envelope + transient-error/epoch-refresh retry policy), and the `RevisionPolledClient` base shared by `AgentClient`/`TopologyClient` |
| `libs/draxul-server/` | Headless server kernel, authoritative revisioned/idempotent topology service, Session-scoped agent discovery, sanitized status projection, profile-resolved managed-agent launch/restore, headless agent inspection/input/restart control, and epoch/runtime-pinned native-session reporting, server-owned periodic/graceful v4 Session checkpoint and cold restore, deterministic fake terminal plus a stable-ID registry of lazy real server-owned shell runtimes, semantic scrollback, per-terminal controller leases, sanitized transport metrics, bounded per-client terminal event queues, and a bounded fair per-Session batch scheduler over the serialized control event loop. `server_kernel.h` is the stable public façade; the grouped state/method inventory and state-thread, mutex, task, lease, generation, cache, and shutdown invariants live in private `src/server_kernel_impl.h`, with façade forwarding isolated in `server_kernel_facade.cpp`. The target deliberately has no window, renderer, host, or product dependency. |
| `libs/draxul-terminal-core/` | Renderer-, window-, and process-free VT state machine, semantic full/dirty/cell snapshots (including the shared `full_grid_update` snapshot→dirty expansion), terminal identity/limits, alternate-screen state, attributes, and reusable scrollback storage |
| `libs/draxul-terminal-process/` | UI-free PTY/ConPTY process adapters owned by server terminal runtimes |

These targets must not depend on product modules. Configure-time checks in
`cmake/CheckDependencyBoundaries.cmake` enforce that direction and the direct
`draxul-host` dependencies needed by its public and implementation headers.
Mounted native products register their module, manifest, runtime payload,
product targets, and focused test targets through `cmake/DraxulPlugins.cmake`.
That file owns platform-neutral staging and accepts shared C++ dependencies only
through the explicit `Draxul::PluginSupport::*` allowlist. It also verifies that
the allowlisted leaves cannot reach broader core libraries. Root CMake can enable
a mounted plugin without knowing its shader names or packaging layout; removing
the plugin directory therefore cannot leave reverse product dependencies in
core. The shared C++ support is statically linked into a same-build plugin and
does not change the runtime boundary, which remains `Draxul::PluginSDK`'s C ABI.
`draxul-host` privately consumes `draxul-client` for the `RemoteTerminalHost`
renderer adapter, including client-local viewport/selection/mouse/paste behavior.
The client-local terminal surface (selection, copy-on-select, copy mode, mouse
reporting hand-off, hyperlink activation, pixel→cell mapping) lives once in
`TerminalSurfaceHostBase`, which sits between `GridHostBase` and both
`TerminalHostBase` (hence `LocalTerminalHost`) and `RemoteTerminalHost`.
It also owns the generic `PluginHost` and its Vulkan/Metal render-pass adapters;
plugin modules remain dynamically linked and are never product dependencies of the
server. The spinning-triangle is the first module staged through the generic
registration contract. SatView, MegaCity/BioView, and ScoreView now use the same
strict contract and only consume their own targets, third-party targets, or the
named plugin-support leaves.
The process adapter, client, and server libraries remain free of host, window,
renderer, font, SDL, and product dependencies.

Shared *shader* code follows the same allowlist idea in a single directory:
`shaders/include/` holds GLSL and MSL includes that any mounted product may
consume (currently the ACES tone-map curve). Products reach it through
`draxul_shared_shader_includes()` in `cmake/DraxulPlugins.cmake`, which returns
the include directory for `glslc -I` / `xcrun metal -I` and the file list for
`add_custom_command(DEPENDS ...)`. Each shared include has a declarative
contract under `shaders/contracts/`, and `tests/shader_abi_parity_tests.cpp`
re-derives both language copies from source and asserts they match it.

### libs/draxul-window/

SDL windowing and platform-facing input/display behavior.

Owns:
- window creation
- DPI/display queries
- event pump / wake behavior
- clipboard / IME / title / focus plumbing

Good place for:
- platform window behavior
- SDL event translation

### libs/draxul-renderer/

Public renderer API plus Vulkan/Metal backends.

Owns:
- renderer implementation hierarchy (`IBaseRenderer` → `IGridRenderer`)
- shared renderer CPU-side state
- GPU upload/submission code
- frame capture for render snapshots

Renderer hierarchy:
```
IBaseRenderer           ← swapchain, device, frame lifecycle, resize, typed render passes
└── IGridRenderer       ← grid cells, atlas, cursor, overlay, font metrics
```

Good place for:
- draw/update behavior
- backend-specific GPU work
- readback/capture paths
- new render pass implementations (implement `IRenderPass::record(IRenderContext&)`)

The generic `IBaseRenderer`/`IFrameContext`/`IRenderPass` contract and typed
Vulkan/Metal render contexts live in `libs/draxul-plugin-support`; the renderer
implements that leaf contract but plugins do not inherit its window, swapchain,
grid, capture, or presentation dependencies.

### libs/draxul-font/

Text pipeline and atlas management.

Owns:
- primary/fallback font loading
- shaping
- glyph cache / atlas population
- text service API
- the shared CPU cluster blit (`cluster_blit.h`) used by tooltip rendering and
  the text-atlas builder (wide clusters advance two cells)

Good place for:
- fallback/font selection
- glyph rasterization
- color emoji support

### libs/draxul-grid/

The terminal cell model.

Owns:
- cell storage
- dirty tracking
- scroll/copy/clear behavior

Good place for:
- redraw correctness
- cell mutation rules

### libs/draxul-nvim/

Embedded Neovim process, RPC, redraw parsing, and input encoding.

Owns:
- child process lifecycle
- msgpack-RPC transport
- UI event parsing
- keyboard/mouse/text input encoding

Good place for:
- Neovim API/event handling
- transport behavior
- input fidelity

### Remaining core libraries

| Directory | Ownership |
|---|---|
| `libs/draxul-http/` | Cross-platform HTTP transport (WinHTTP on Windows, Foundation on macOS) |
| `libs/draxul-config/` | Config schema, TOML document I/O, and keybinding parsing |
| `libs/draxul-gui/` | GPU-grid-native overlays such as palettes, tooltips, and toasts; no ImGui frame loop |
| `libs/draxul-ui/` | ImGui diagnostics and developer-facing UI (key translation lives in `libs/draxul-imgui-core`) |
| `libs/draxul-runtime-support/` | Shared grid-render pipeline, printing, resource monitoring, and background UI requests |
| `libs/draxul-host/` | UI/process host adapters, terminal/Neovim hosts, PTY/ConPTY behavior, selection, copy mode, and client-side terminal presentation |
| `libs/draxul-nanovg/` | Two targets: `draxul-nanovg-backend` (NanoVG core + custom Vulkan/Metal backends and their GLSL shaders; leaf-narrow, exported as `Draxul::PluginSupport::NanoVG` with a settable shader root for plugin hosts) and `draxul-nanovg` (the in-process `INanoVGPass` render-pass integration) |
| `libs/draxul-render-test/` | Render-test driver and reusable render-test hosts |
| `libs/draxul-app-support/` | Interface target bundling reusable config/runtime/render-test dependencies |

## Product Modules

| Directory | CMake gate | Main targets and responsibility |
|---|---|---|
| `modules/markdown/` | Always built | `draxul-markdown` parses/layouts documents; `draxul-markdown-host` integrates the native pane and platform render pass |
| `modules/kanban/` | Always built | `draxul-kanban` owns board storage, layout/navigation, and the native Kanban host |
| `plugins/megacity/` | `DRAXUL_ENABLE_MEGACITY` | Submodule → [draxul-megacity](https://github.com/cmaughan/draxul-megacity). Self-contained MegaCity/BioView product: code semantics, Tree-sitter, geometry, scene, Vulkan/Metal renderer, UI, shaders, assets, tests, and dynamic module |
| `plugins/satview/` | `DRAXUL_ENABLE_SATVIEW` | Submodule → [draxul-satview](https://github.com/cmaughan/draxul-satview). Self-contained satellite product: core, scene, services, runtime, Vulkan/Metal renderer, shaders, assets, tests, and the dynamic module |
| `plugins/scoreview/` | `DRAXUL_ENABLE_SCOREVIEW` | Submodule → [draxul-scoreview](https://github.com/cmaughan/draxul-scoreview). Self-contained notation, learning, transport, worker, MIDI/audio/microphone, UI, NanoVG Vulkan/Metal rendering, assets, tests, and dynamic module |

All three optional product directories own their third-party dependency
declarations, sanitizer/coverage target lists, shader compilation, package
payload, test source inventory, and focused CTest wiring. The root build contains
only their feature switches and mount-point `add_subdirectory` calls. SatView's
catalog and texture generators likewise live under `plugins/satview/tools/`, so
the product can become a submodule without leaving maintenance scripts in core.
Each mount point is a cache path (`DRAXUL_MEGACITY_PLUGIN_DIR`,
`DRAXUL_SATVIEW_PLUGIN_DIR`, or `DRAXUL_SCOREVIEW_PLUGIN_DIR`). An enabled but
absent checkout is reported and skipped, making no-product and partial-submodule
trees supported configurations rather than configure errors.
Their registrations use strict dependency checking: SatView and MegaCity/BioView
consume the generic plugin runtime, render, configuration, text, HTTP,
performance, tooltip/ImGui, and platform GPU leaves as needed; ScoreView keeps
its standalone product stack and uses the same in-tree registration/support
naming. None of the product targets links `draxul-host`, the renderer
implementation, app orchestration, or another product.

Markdown and Kanban are linked directly into `draxul`. MegaCity/BioView, SatView,
and ScoreView are staged and loaded only as native modules. Core has no product
host kinds, provider factories, renderer bridge, or compiled-in fallback.

## Generated Views

### Target graph

[deps.svg](deps/deps.svg)

This file is generated from a configured build and may reflect the options used for
that documentation build. Use it when:
- checking module boundaries
- spotting unexpected library dependencies
- deciding where a new abstraction belongs

### Class diagram

[draxul_classes.svg](uml/draxul_classes.svg)

Use this when:
- exploring object relationships inside a subsystem
- understanding which class owns a responsibility

### Local API docs

Generated locally at `docs/api/index.html` via Doxygen.

Use this when:
- you want browsable symbol and header docs
- you want include/call/reference graphs tied to public headers
- you want a more traditional API-reference view than the diagrams provide

## Validation Map

### Fast confidence

- `python do.py test debug` — build and run core unit shards in the shared Debug cache
- `python do.py test debug --megacity|--satview|--scoreview` — add only an affected product suite
- `python do.py test debug --products` — add every product suite for shared plugin seams
- `python do.py test debug --all` — explicit complete unit inventory
- `python do.py smoke --skip-build` — startup-check that already-built app
- `python do.py run release` — final Release build and startup confirmation

### Deterministic UI confidence

- `python do.py basic`
- `python do.py cmdline`
- `python do.py unicode`
- `python do.py blessall`

### Documentation / hero image

- `python do.py shot`

## Planning Links

- Active work items: [kanban/pending/](../kanban/pending/)
- Design notes: [plans/design](../plans/design)
- Review notes: [plans/reviews](../plans/reviews)
- Learnings: [docs/learnings/learnings.md](learnings/learnings.md)

## Practical Heuristics

- If the issue is about what Neovim sent or how input is encoded, start in `draxul-nvim`.
- If the issue is about what the screen should contain, start in `draxul-grid`.
- If the issue is about how the screen is drawn, start in `draxul-renderer`.
- If the issue is about glyph choice, shaping, emoji, tofu, or atlas behavior, start in `draxul-font`.
- If the issue is about DPI, focus, clipboard, IME, or visible window behavior, start in `draxul-window`.
- If the issue is about config parsing/validation, start in `draxul-config`.
- If the issue is about VT parsing, terminal modes, semantic cells, alternate-screen
  behavior, or reusable scrollback storage, start in `draxul-terminal-core`.
- If it is about a shell process, PTY/ConPTY lifecycle, selection, copy mode, or
  client presentation, start in `draxul-host`.
- If the issue belongs to SatView, MegaCity/BioView, or ScoreView, start in its
  directory under `plugins/`. Markdown and Kanban remain under `modules/`.
- If the issue crosses several modules, start in `app/` to trace orchestration, then move reusable logic downward. Shared Session identity/split projection belongs in `draxul-client::TopologyProjection`; `app/` only adapts it to controllers and pane hosts.

## Why This Exists

As the repo grows, humans need a small number of reliable views into its structure:

- a module map
- generated dependency/class diagrams
- clear validation entry points
- current planning documents

Without that, the architecture becomes harder to hold in your head, even if the code itself stays clean.
