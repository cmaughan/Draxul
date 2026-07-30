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
└── optional product hosts: draxul-megacity / draxul-satview-host / draxul-scoreview-host
        │
        ├── product-specific model, scene, service, and renderer targets
        └── shared host / renderer / UI infrastructure
                │
                ├── draxul-host (including RemoteTerminalHost) /
                │   draxul-runtime-support / draxul-render-test
                ├── draxul-config / draxul-gui / draxul-ui / draxul-nanovg
                └── draxul-http / draxul-font / draxul-grid / draxul-nvim /
                    draxul-renderer / draxul-window
                            │
                            └── narrow foundations: draxul-types / draxul-performance /
                                draxul-agent / draxul-session-model / draxul-bmp / draxul-host-identity /
                                draxul-terminal-core / draxul-terminal-process
```

This is a dependency shape, not a promise that every target in one row links every
target below it. Consult the target's `CMakeLists.txt` for exact public/private edges.
In particular, `draxul-app` deliberately has no source-level dependency on optional
product modules; the executable links and registers those host providers.

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

Good place for:
- narrow shared contracts
- POD-like data passed between modules

Compiled BMP I/O, runtime performance collection, and host/provider identity are
deliberately not owned here. They live in the narrowly named `draxul-bmp`,
`draxul-performance`, and `draxul-host-identity` targets so changing those facilities
does not rebuild or extend the universal value-type archive.

### Foundation support targets

| Directory | Ownership |
|---|---|
| `libs/draxul-performance/` | Runtime timing collection and the `PERF_MEASURE` instrumentation API |
| `libs/draxul-bmp/` | RGBA frame BMP read/write only; depends on frame value types and performance support |
| `libs/draxul-host-identity/` | Neutral `HostKind` identity/parsing contract shared by host and runtime APIs |
| `libs/draxul-agent/` | Neutral agent identity/profile/runtime values plus bundled, versioned terminal-status and process-discovery evaluators |
| `libs/draxul-session-model/` | Renderer-free durable Session/Space/tab/pane snapshot values, validation, versioned TOML codec, and transactional file replacement shared by the app and server |
| `libs/draxul-control/` | Versioned, authenticated local Session control transport and client (Windows named pipe; Unix-domain socket elsewhere) |
| `libs/draxul-protocol/` | Renderer- and transport-neutral server hello/status, versioned topology and sanitized agent-projection values, plus terminal pane, snapshot, delta, controller, clipboard, paged-scrollback, and diagnostic values |
| `libs/draxul-client/` | Singleton discovery/launch/status/shutdown plus renderer-free remote-terminal and sanitized-agent polling, acknowledged/coalesced Session delivery, and `TopologyProjection`, which owns remote/local Space-tab-pane identities, stable leaf/split projection, structural signatures, divider-node mapping, and topology command activation bookkeeping behind the app's thin controller/PaneManager adapters; also owns the scrollback-page client, attach-latency metric, and headless probe API |
| `libs/draxul-server/` | Headless server kernel, authoritative revisioned/idempotent topology service, Session-scoped agent discovery, sanitized status projection, profile-resolved managed-agent launch/restore, headless agent inspection/input/restart control, and epoch/runtime-pinned native-session reporting, server-owned periodic/graceful v3 Session checkpoint and cold restore, deterministic fake terminal plus a stable-ID registry of lazy real server-owned shell runtimes, semantic scrollback, per-terminal controller leases, sanitized transport metrics, bounded per-client terminal event queues, and serialized control event loop; deliberately has no window, renderer, host, or product dependency |
| `libs/draxul-terminal-core/` | Renderer-, window-, and process-free VT state machine, semantic full/dirty/cell snapshots, terminal identity/limits, alternate-screen state, attributes, and reusable scrollback storage |
| `libs/draxul-terminal-process/` | UI-free PTY/ConPTY process adapters shared by local terminal hosts and server-owned terminal runtimes |

These targets must not depend on product modules. Configure-time checks in
`cmake/CheckDependencyBoundaries.cmake` enforce that direction and the direct
`draxul-host` dependencies needed by its public and implementation headers.
`draxul-host` privately consumes `draxul-client` for the `RemoteTerminalHost`
renderer adapter, including client-local viewport/selection/mouse/paste behavior, and
`draxul-terminal-process` for local shells. The process adapter,
client, and server libraries remain free of host, window, renderer, font, SDL, and
product dependencies.

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
- renderer interface hierarchy (`IBaseRenderer` → `I3DRenderer` → `IGridRenderer`)
- render pass abstraction (`IRenderPass` / `IRenderContext`) replacing legacy `void*` callbacks
- shared renderer CPU-side state
- GPU upload/submission code
- frame capture for render snapshots

Renderer hierarchy:
```
IBaseRenderer           ← swapchain, device, begin_frame, end_frame, resize
└── I3DRenderer         ← render pass registration (register_render_pass / unregister_render_pass)
    └── IGridRenderer   ← grid cells, atlas, cursor, overlay, font metrics
```

Good place for:
- draw/update behavior
- backend-specific GPU work
- readback/capture paths
- new render pass implementations (implement `IRenderPass::record(IRenderContext&)`)

### libs/draxul-font/

Text pipeline and atlas management.

Owns:
- primary/fallback font loading
- shaping
- glyph cache / atlas population
- text service API

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
| `libs/draxul-ui/` | ImGui diagnostics and developer-facing UI |
| `libs/draxul-runtime-support/` | Shared grid-render pipeline, printing, resource monitoring, and background UI requests |
| `libs/draxul-host/` | UI/process host adapters, terminal/Neovim hosts, PTY/ConPTY behavior, selection, copy mode, and client-side terminal presentation |
| `libs/draxul-nanovg/` | Cross-platform NanoVG render-pass integration |
| `libs/draxul-render-test/` | Render-test driver and reusable render-test hosts |
| `libs/draxul-app-support/` | Interface target bundling reusable config/runtime/render-test dependencies |

## Product Modules

| Directory | CMake gate | Main targets and responsibility |
|---|---|---|
| `modules/markdown/` | Always built | `draxul-markdown` parses/layouts documents; `draxul-markdown-host` integrates the native pane and platform render pass |
| `modules/kanban/` | Always built | `draxul-kanban` owns board storage, layout/navigation, and the native Kanban host |
| `modules/megacity/` | `DRAXUL_ENABLE_MEGACITY` | Code semantics/tree-sitter, geometry, scene, renderer, host helpers, and the `draxul-megacity` product host; see its nested `AGENTS.md` |
| `modules/satview/` | `DRAXUL_ENABLE_SATVIEW` | Satellite core, scene, services, renderer, and `draxul-satview-host` |
| `modules/score/` | `DRAXUL_ENABLE_SCOREVIEW` | Notation, learning, MIDI/audio input, ScoreView model, and `draxul-scoreview-host` |

Markdown and Kanban are linked directly into `draxul`. Optional product hosts are
also linked only at the executable boundary when their CMake option is enabled.

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

- `python do.py smoke`
- `python do.py test`

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
- If the issue belongs only to Markdown, Kanban, Megacity, SatView, or ScoreView, start in that product's `modules/` directory.
- If the issue crosses several modules, start in `app/` to trace orchestration, then move reusable logic downward. Shared Session identity/split projection belongs in `draxul-client::TopologyProjection`; `app/` only adapts it to controllers and pane hosts.

## Why This Exists

As the repo grows, humans need a small number of reliable views into its structure:

- a module map
- generated dependency/class diagrams
- clear validation entry points
- current planning documents

Without that, the architecture becomes harder to hold in your head, even if the code itself stays clean.
