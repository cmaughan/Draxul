I'll inspect the repository structure, CMake targets, and key modules for a hygiene/modularity review only—no edits or builds.Pending kanban already covers several library splits; I'll read those items and probe CMake/source for additional structural issues.I'll dig into oversized units, CMake ownership, and seams not already covered by pending cards.# Refactor Review — Code Hygiene & Module Boundaries

Review-only. Inspected current `app/`, `libs/`, `modules/`, `shaders/`, `tests/`, `scripts/`, root CMake, and agent guides. **Excluded** items already in `kanban/pending/` (00–08), `kanban/ice-box/` (e.g. 22 tab/session controllers, 125 overlay registry, 25 renderer residual, 124 Linux dead paths), and `kanban/done/`.

---

## Findings (by leverage / sequence)

### 1. `draxul-runtime-support` is a high-fan-out grab-bag

| | |
|---|---|
| **Location** | `libs/draxul-runtime-support/CMakeLists.txt` (PUBLIC links L13–22); sources `grid_rendering_pipeline.*`, `ui_request_worker.*`, `pane_print*`, `system_resource_monitor.*`; header `include/draxul/app_options.h` |
| **Priority** | **P1** |
| **Current structural problem** | One STATIC target publicly depends on **font + grid + nvim + renderer + protocol + window + config**. Consumers of any one piece (e.g. system monitor, app options) inherit the full GPU/Nvim closure. `UiRequestWorker` public header includes `nvim_rpc.h`; `GridRenderingPipeline` includes `renderer.h`; `AppOptions` includes `renderer.h`, `window.h`, `server_protocol.h`. Responsibilities are unrelated except “used by the app.” |
| **Proposed boundary** | Split into narrow static libs: `draxul-grid-render-pipeline` (grid+font+renderer), `draxul-ui-request-worker` (nvim channel only; or fold into nvim-transport after pending 03), `draxul-pane-print` (platform print), `draxul-system-monitor` (standalone), `draxul-app-options` (value + factories without pulling implementation). Keep an optional INTERFACE aggregate only if needed for migration. |
| **Dependency shape** | Callers: host grid path, App, tests. Deps: each lib links only its real needs. Public API: one header family per concern. Private: macOS PDF/AppKit print `.mm`. |
| **Migration path** | (1) Add targets with same sources. (2) Retarget `draxul-host` / `draxul-app` / tests one edge at a time. (3) Delete grab-bag or leave as INTERFACE re-export. Keep tree green each step. |
| **Testing improvement** | Ligature/dirty pipeline tests link pipeline-only; monitor unit tests avoid renderer; print tests optional/platform. |
| **Agent-work benefit** | Touching print or system metrics no longer rebuilds nvim/renderer; ownership is obvious. |
| **Risks** | Coordinate with ice-box UiRequestWorker evaluation; preserve Windows/macOS print paths. |

---

### 2. `ServerKernel::Impl` remains a multi-thousand-line orchestration TU

| | |
|---|---|
| **Location** | `libs/draxul-server/src/server_kernel.cpp` (~3590 lines; `ServerKernel::Impl` ~L339+); services already exist as `topology_service.*`, `remote_terminal_service.*`, `server_agent_service.*`, `session_topology_bridge.*` |
| **Priority** | **P1** |
| **Current structural problem** | Service classes exist, but kernel still owns discovery/singleton metadata, control listener loop, terminal registry wiring, checkpoint cadence, agent mutation bookkeeping, and transport serialization in one Impl. Pending **bugs** touch this file constantly → high merge collision and broad test surface for any server change. |
| **Proposed boundary** | Keep `ServerKernel` as thin facade. Extract private collaborators as STATIC or internal TUs: `ServerDiscovery` / published-metadata lease, `ServerControlLoop` (request dispatch only), `ServerCheckpointScheduler`, `ServerTerminalRegistry` wiring (not re-implement services). No new public product API unless needed. |
| **Dependency shape** | Callers: `draxul` server entry, tests. Deps: existing `draxul-control`, `draxul-protocol`, `draxul-session-model`, `draxul-terminal-process`. Public: `server_kernel.h` unchanged. Private: new `src/*` collaborators. |
| **Migration path** | Move pure helpers first (status snapshot, terminal display names), then control-loop body, then discovery/eviction. One PR per collaborator; keep `server_kernel_tests` green. |
| **Testing improvement** | Unit-test discovery/eviction and checkpoint scheduling without full kernel; keep integration tests on kernel facade. |
| **Agent-work benefit** | Lifecycle bugs vs topology vs agent control can own separate files; less contention on `server_kernel.cpp`. |
| **Risks** | Threading/stop semantics; Windows named-pipe vs Unix socket; do not reintroduce host/renderer deps (already enforced). |

---

### 3. Shell chrome still lives inside `draxul-app` after internal extraction

| | |
|---|---|
| **Location** | `app/chrome_host.*`, `chrome_layout.*`, `chrome_pill.*`, `chrome_text_layer.*`, `chrome_vector_pass.*`, `rename_editor.*`, `weather_service.*`; linked via root `CMakeLists.txt` `draxul-app` sources L281–314 |
| **Priority** | **P1** |
| **Current structural problem** | Done work split chrome **internally**, but the cluster still compiles into `draxul-app` next to topology/session/control orchestration. Any chrome/layout/weather edit rebuilds the full app archive; `draxul-test-app` always pays that cost. Cohesive UI shell is not a library boundary. |
| **Proposed boundary** | `draxul-chrome` STATIC: layout + rename + vector/text passes + host assembly. Optional `draxul-weather` if HTTP weather should stay free of chrome. App only constructs `ChromeHost` and supplies `Deps` callbacks. |
| **Dependency shape** | Callers: `draxul-app`, chrome-focused tests. Deps PUBLIC: `draxul-gui`/`draxul-nanovg`/`draxul-renderer`/`draxul-host` contracts as today; PRIVATE: http for weather if co-located. Public headers under `include/draxul/chrome/` (or keep relative includes during migrate). |
| **Migration path** | Move sources + headers; change `draxul-app` to link `draxul-chrome`; rehome chrome/rename tests to a small target or keep in app tests via link. |
| **Testing improvement** | Layout/hit-region tests link chrome without control-plane/topology code. |
| **Agent-work benefit** | Parallel chrome work vs session/topology agents; smaller compile units. |
| **Risks** | DPI/theme parity Windows/macOS; keep NanoVG/grid text path identical. |

---

### 4. `draxul-test-app` always links optional product hosts

| | |
|---|---|
| **Location** | `tests/CMakeLists.txt` L274–292 (`draxul-test-app` + conditional `draxul-megacity` / `draxul-satview-host` / `draxul-scoreview-host`) |
| **Priority** | **P1** |
| **Current structural problem** | App/session/chrome unit shards rebuild and link full optional product graphs whenever those CMake options are ON (the default). Most app tests do not need product hosts; a few registration/smoke paths do. This defeats modular test isolation for the largest day-to-day agent surface. |
| **Proposed boundary** | Default `draxul-test-app` links only core app + always-on markdown/kanban. Move product-provider registration tests to `draxul-test-megacity` / `satview` / `scoreview-host` (or a tiny `draxul-test-app-products` executable gated by options). |
| **Dependency shape** | App tests → `draxul-app` (+ markdown/kanban). Product registration → product test targets already present. |
| **Migration path** | Inventory which app test files use product hosts; reclassify; drop optional links from default app target. |
| **Testing improvement** | Faster focused app CTEst; product host registration still covered in product labels. |
| **Agent-work benefit** | App agents avoid megacity/satview/score rebuilds; optional-module agents own their registration tests. |
| **Risks** | Don’t drop the only coverage for provider registration; keep `DRAXUL_TEST_REGISTER_APP_HOSTS` semantics explicit. |

---

### 5. Core host callback contract embeds Markdown product behavior

| | |
|---|---|
| **Location** | `libs/draxul-host/include/draxul/host.h` `IHostCallbacks::show_markdown_preview` / `hide_markdown_preview` / `is_markdown_preview_visible` (~L152–171); `HostReloadConfig::markdown_font_size` / `markdown_margin_columns` (~L85–86); App overrides `app/app.h` L162–164, `app/app.cpp` ~2535–2660; `PaneManager` markdown companion APIs |
| **Priority** | **P1** |
| **Current structural problem** | Every host, fake host, and product host inherits Markdown companion-pane and markdown config fields through the **foundation** host API. Product-specific orchestration is forced into App/`draxul-host` rather than `modules/markdown`. Violates “products stay out of foundations” (enforced for link edges but not for callback surface). |
| **Proposed boundary** | Replace product methods with a narrow extension seam, e.g. `IHostCallbacks::query_extension<T>()` / optional `ICompanionPaneService*`, or App-only router registered by markdown host provider. Move markdown fields off `HostReloadConfig` into markdown host config reload path. |
| **Dependency shape** | `draxul-host-api` (pending 02) stays product-free. Markdown module registers companion behavior at executable boundary. |
| **Migration path** | Add generic companion API; migrate Kanban/App call sites; default no-op on old methods then remove; update fakes/tests. Best after pending host-api split. |
| **Testing improvement** | Host fakes shrink; markdown companion tests live under markdown/kanban targets without bloating core host isolation. |
| **Agent-work benefit** | Host-layer agents ignore markdown; markdown agents own companion pane lifecycle. |
| **Risks** | Kanban card preview UX must remain; remote topology projection of companion panes needs careful mapping. |

---

### 6. `AppOptions` / launch types sit on the wrong dependency shelf

| | |
|---|---|
| **Location** | `libs/draxul-runtime-support/include/draxul/app_options.h` (includes renderer, window, server protocol, host factories) |
| **Priority** | **P2** |
| **Current structural problem** | “Options” is a cross-cutting composition type, not runtime-support. Pulling it in via runtime-support couples CLI/config/tests to renderer+window even when only session/control flags are needed. Amplifies finding 1. |
| **Proposed boundary** | `draxul-app-options` (or place under `draxul-client`/`draxul-config` split): pure launch/session flags vs factory hooks. Factories that need renderer stay in app-facing header. |
| **Dependency shape** | CLI + App + tests depend on options; options depend on protocol/host-identity value types, not full runtime-support. |
| **Migration path** | Move header; fix includes; leave thin wrapper include for one release if needed. |
| **Testing improvement** | CLI/session option tests without GPU link. |
| **Agent-work benefit** | Clear owner for launch surface vs grid pipeline. |
| **Risks** | Low if mechanical; watch circular deps with client recovery types. |

---

### 7. Multi-target product modules share one public include tree

| | |
|---|---|
| **Location** | `modules/markdown/draxul-markdown/include/draxul/markdown/` (core + `markdown_host.h` + render pass); `modules/score/draxul-scoreview/include/draxul/scoreview/` shared by `draxul-scoreview` and `draxul-scoreview-host` (`CMakeLists.txt` both `PUBLIC include`) |
| **Priority** | **P2** |
| **Current structural problem** | Core consumers see host/render headers; host and core cannot enforce include isolation at the filesystem level. Link isolation cannot fully compensate. Differs from satview’s clearer host/core/scene include split. |
| **Proposed boundary** | `include/draxul/markdown/` core-only; `include/draxul/markdown/host/` or host-private `src/` headers for host/render pass. Same pattern for scoreview host headers (`score_host.h` already host-owned in sources but published from shared tree). |
| **Dependency shape** | Core target PUBLIC core includes only; host target PUBLIC host includes + core. |
| **Migration path** | Move headers; update includes; add link-isolation consumers for markdown core / score core. Complements pending 04/07 without replacing them. |
| **Testing improvement** | Core tests fail to compile if they include host headers accidentally. |
| **Agent-work benefit** | Clearer “edit core vs host” scope; fewer accidental host deps. |
| **Risks** | Mechanical include churn; keep namespaces stable. |

---

### 8. Product shaders and staging are root-owned collision hotspots

| | |
|---|---|
| **Location** | `shaders/` (grid/gui + `megacity_*` + `satview_*` + `markdown_*`); root `CMakeLists.txt` Metal staging L421–466 / Windows shader copy L467–476; `cmake/CompileShaders*.cmake` |
| **Priority** | **P2** |
| **Current structural problem** | Optional product shader edits and core grid shader edits share one directory and root executable staging graph. Agents collide on root CMake and the global shader compile targets; product OFF builds still reason about mixed trees. |
| **Proposed boundary** | Per-module shader dirs (`modules/megacity/shaders`, `modules/satview/shaders`) with module CMake owning compile + `stage_*` targets; root only stages core grid/gui/nanovg. |
| **Dependency shape** | Product host/renderer targets depend on their shader stage targets; `draxul` aggregates when option ON. |
| **Migration path** | Move files; update compile scripts; preserve output paths under `$<TARGET_FILE_DIR:draxul>/shaders` for runtime compatibility. |
| **Testing improvement** | Product-only shader rebuild; clearer dependency edges in GraphViz. |
| **Agent-work benefit** | Megacity/SatView shader work stays under module ownership; core renderer agents avoid product noise. |
| **Risks** | Metal/Vulkan compile path parity; keep existing runtime relative paths. |

---

### 9. `draxul-app` publishes the entire `app/` tree as PUBLIC includes

| | |
|---|---|
| **Location** | Root `CMakeLists.txt` L316: `target_include_directories(draxul-app PUBLIC app)` |
| **Priority** | **P2** |
| **Current structural problem** | No public vs private app API. Tests and future libs that link `draxul-app` can include any orchestration header (`app.h`, control internals, chrome, etc.), freezing accidental coupling. Blocks safe extraction of chrome/session libraries. |
| **Proposed boundary** | After chrome/session extractions: PUBLIC only stable façade headers (if any); PRIVATE for `app.cpp` internals. Prefer libraries over a fat app public surface. |
| **Dependency shape** | Executable + integration tests link `draxul-app`; unit tests link narrow libs. |
| **Migration path** | Pair with finding 3 and ice-box 22; introduce `app/include` only for intentional API. |
| **Testing improvement** | Forces tests onto controller/library seams (`AppDeps`, controllers) instead of private App members (via `AppTestAccess` where still needed). |
| **Agent-work benefit** | Harder to create cross-cutting includes; clearer ownership. |
| **Risks** | Short-term test include fixes; do not mix with behavior changes. |

---

### 10. Hollow `draxul-app-support` + stale ownership pointers

| | |
|---|---|
| **Location** | `libs/draxul-app-support/CMakeLists.txt` (INTERFACE only → config/runtime/render-test); `docs/module-map.md` L214; multiple `kanban/ice-box/*` still cite `libs/draxul-app-support/src/{split_tree,input_dispatcher,grid_rendering_pipeline,app_config_io}.cpp` |
| **Priority** | **P2** |
| **Current structural problem** | The target no longer owns sources; real code lives in `app/` and `draxul-runtime-support` / `draxul-config`. Agents following ice-box paths land on wrong trees. Overlaps pending **08** guidance repair but the empty target itself is structural dead weight. |
| **Proposed boundary** | Either delete INTERFACE and link real deps directly, or repurpose as the real home for extracted app-shell libraries (chrome/session)—not a synonym for “misc.” |
| **Dependency shape** | Tests currently link `draxul-app-support` for convenience (`tests/CMakeLists.txt` L251)—replace with explicit deps. |
| **Migration path** | Update consumers; remove or redefine target; fix ice-box path prose when editing those cards. |
| **Testing improvement** | Explicit deps surface accidental coupling. |
| **Agent-work benefit** | Stops false module ownership. |
| **Risks** | Low; coordinate wording with pending 08. |

---

### 11. Orphan `LocalTerminalHost` duplicates client terminal presentation

| | |
|---|---|
| **Location** | `libs/draxul-host/include/draxul/local_terminal_host.h`, `src/local_terminal_host.cpp` (~1000 lines); no subclasses; factory only registers Nvim (`host_factory.cpp`); production shells use `RemoteTerminalHost` |
| **Priority** | **P2** |
| **Current structural problem** | Selection/copy-mode/mouse/scrollback UI logic lives in a “local process” host that is not constructed in production, while remote presentation reimplements the client path. Inflates `draxul-host`, confuses agents about server-owned shells, and fights the pending host split. |
| **Proposed boundary** | Fold shared presentation into one client terminal presentation module (pending 02’s remote presentation target); delete or demote LocalTerminalHost to test-only fixture if still needed. |
| **Dependency shape** | Presentation → terminal-core + client; process I/O stays server-side. |
| **Migration path** | Diff Local vs Remote feature matrix; port any unique behavior; remove dead type; update tests that mention it. |
| **Testing improvement** | One presentation suite; no dual maintenance. |
| **Agent-work benefit** | Matches real architecture (server PTY, client RemoteTerminalHost). |
| **Risks** | Don’t drop features still only on Local path; verify with terminal characterization tests. |

---

## Already tracked (do not re-queue)

| Card | Why it still matters for sequencing |
|---|---|
| pending **00** internal-target policy | Blocks every new STATIC target |
| pending **01** `app/CMakeLists.txt` | Prerequisite for chrome lib + app private includes |
| pending **02** host API / grid / terminal split | Foundation for findings 5, 11 |
| pending **03** nvim protocol/transport | Unblocks UiRequestWorker placement |
| pending **04–07** kanban/megacity/satview/score splits | Product isolation already planned |
| pending **08** guidance + `do.py test --label` | Fixes CLAUDE.md stale `I3DHost` claims, megacity `draxul-tests.exe` paths, missing satview/score AGENTS |
| ice-box **22** App session projection controller | Still needed: TabController exists, but remote apply/agent projection remains in `App` (`app.cpp` ~3014–4200+) |
| ice-box **125** overlay registry | Orthogonal overlay host lifecycle |

---

## Proposed module / target map (delta only)

```text
draxul (exe)
├── draxul-app                    # loop, wire-up only
│   ├── draxul-chrome             # NEW (finding 3)
│   ├── draxul-app-options        # NEW (finding 6)
│   └── (session projection lib)  # ice-box 22 when promoted
├── draxul-host-api / grid-host / terminal-presentation / nvim-*  # pending 02–03
├── draxul-grid-render-pipeline   # NEW from runtime-support
├── draxul-system-monitor         # NEW
├── draxul-pane-print             # NEW
├── draxul-server
│   └── private: discovery, control-loop, checkpoint  # finding 2
└── products (existing multi-lib graphs; include trees tightened — finding 7)
        shaders owned per module — finding 8
```

Remove or redefine hollow `draxul-app-support`.

---

## Best parallel agent work packages

1. **Build policy** (pending 00 alone) — serial gate.  
2. After 00 freezes: **runtime-support split** ‖ **server kernel collaborators** ‖ **test-app product unlink** (findings 1, 2, 4).  
3. After 01: **chrome library** (finding 3).  
4. After 02 host-api: **Markdown off IHostCallbacks** + **LocalTerminalHost retirement** (5, 11).  
5. Product tracks continue on pending 04–07; **shader ownership** (8) pairs with those modules.  
6. **Guidance/label tooling** (pending 08) last, naming final targets.  
7. Promote ice-box **22** once topology tests are green—largest remaining `App` isolation win, already designed.

Avoid parallel edits to: root `CMakeLists.txt` source lists, `server_kernel.cpp`, `app/app.cpp`, `host.h`.

---

## Strongest structural qualities to preserve

- **Configure-time dependency firewall** (`cmake/CheckDependencyBoundaries.cmake`) and renderer-free client/server/protocol/terminal stack.  
- **Focused Catch2 executables** with exact source partition + inventory baselines (`tests/CMakeLists.txt`).  
- **Link-isolation executables** for foundation public headers.  
- **Optional product gates** with registration only at the executable (`draxul-app` deliberately not linking megacity).  
- **Markdown core/host** and **Score/SatView multi-lib** precedents (scene/core/host).  
- **TopologyProjection in `draxul-client`** (done 27)—right pattern for further App thinning.  
- **AppDeps / controller extractions already landed** (`TabController`, `SpaceController`, `PaneManager`, `InputDispatcher`, `GuiActionHandler`, chrome layout split).  
- **Backend-private Vulkan resources** + test-internals INTERFACE pattern for white-box tests without widening production includes.

---

*End of review.*
