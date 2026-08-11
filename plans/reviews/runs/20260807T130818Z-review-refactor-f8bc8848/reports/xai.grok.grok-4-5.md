I'll inspect the repository structure, CMake targets, and key source areas for a refactoring-focused review—no edits or builds.Pending kanban already covers several refactors—I'll read those and dig into structure that isn't already tracked.Next I'll measure large TUs, CMake dependency edges, and app/test seams for issues beyond the pending cards.Checking a few more dependency edges for accuracy:# Refactor Review — Code Hygiene, Modularity, Testability

Scope: structural review only. Items already in `kanban/pending/00–08`, ice-box controller/overlay cards, and completed boundary work are not re-filed. CMake and include/link edges were checked against the live tree.

---

## Findings (by leverage / sequencing)

### 1. Pure shell geometry still lives inside `draxul-app`

| Field | Detail |
|---|---|
| **Location** | `app/split_tree.{h,cpp}`, `app/app_shell_layout.{h,cpp}`, `app/chrome_layout.{h,cpp}`, `app/chrome_pill.*`, `app/fuzzy_match.{h,cpp}`; tests `tests/split_tree_tests.cpp`, `app_shell_layout_tests.cpp`, `chrome_layout_tests.cpp`, `fuzzy_match_tests.cpp`; app sources listed in root `CMakeLists.txt` (~281–314); `draxul-test-app` links (`tests/CMakeLists.txt` ~274–292) |
| **Priority** | **P1** |
| **Current structural problem** | Layout/match code is pure CPU (session-model / value types only). It is compiled into `draxul-app` and classified under `DRAXUL_APP_TEST_SOURCES`, so every pure geometry edit rebuilds and links the full app graph plus optional product hosts (`draxul-megacity` / `satview-host` / `scoreview-host` when enabled). `target_include_directories(draxul-app PUBLIC app)` also publishes the entire app header tree as the library’s public surface. |
| **Proposed boundary** | `draxul-app-shell` (or repurpose hollow `libs/draxul-app-support/` from INTERFACE into a real STATIC lib): split tree, shell layout, chrome layout/pills, fuzzy match, pure rename geometry. Keep `ChromeHost` / NanoVG / ImGui hosts in `draxul-app`. |
| **Dependency shape** | Callers: `draxul-app`, chrome/host tests. Deps: `draxul-session-model`, `draxul-types`, maybe `draxul-config` types only. **No** window/renderer/host/nvim/server. Public: layout PODs + pure functions. Private: implementation TUs. |
| **Migration path** | 1) Add STATIC target with current pure sources. 2) Link `draxul-app` → shell. 3) Move pure tests to `draxul-test-core` or `draxul-test-app-shell` with shell-only links. 4) Optionally tighten app includes to `include/` vs private headers after pending `01` CMake move. |
| **Testing improvement** | Pure layout/split/fuzzy suites build without GPU, product hosts, or `App`. |
| **Agent-work benefit** | Shell-layout agents no longer collide with topology/`app.cpp`/product registration; narrower compile graph. |
| **Risks** | `chrome_layout.h` includes `system_resource_monitor` snapshot types — keep as value-only include or thin DTO; do not pull runtime-support into the pure shell target. |

---

### 2. `draxul-test-core` remains a mega-link sink

| Field | Detail |
|---|---|
| **Location** | `tests/CMakeLists.txt` `draxul-test-core` (~247–269): links `draxul-server`, `draxul-host`, `draxul-nvim`, `draxul-renderer`, `draxul-nanovg`, `draxul-client`, `draxul-protocol`, font/grid/window/ui/gui/runtime-support/render-test, plus renderer test-internals |
| **Priority** | **P1** |
| **Current structural problem** | Modular executables exist (`done/35`), but “core” still means “everything not product/app.” A font, grid, VT, config, or protocol unit change can force the full server/host/renderer link closure. That defeats agent isolation and keeps core rebuilds large. |
| **Proposed boundary** | Second-wave focused targets aligned with real libraries, e.g. `draxul-test-foundation` (types/config/bmp/perf), `draxul-test-terminal` (terminal-core/process), `draxul-test-nvim` (protocol+transport once pending `03` lands), `draxul-test-renderer` (renderer/nanovg/gui), `draxul-test-server-client` (protocol/client/server). Keep aggregate `draxul-tests`. |
| **Dependency shape** | Each executable PUBLIC-links only its owning libraries + `draxul-test-support`. Shared fakes stay in `tests/support/` via the existing INTERFACE target. |
| **Migration path** | Reclassify sources by include/link need (not filename alone); add partition/baseline checks like the current exact-once partition; migrate one domain per PR. Sequence after pending host/nvim splits so target names stabilize. |
| **Testing improvement** | True label-level builds (`core` no longer means “link the world”); faster TDD for foundation work. |
| **Agent-work benefit** | Parallel agents own separate test binaries and link graphs; fewer false rebuilds. |
| **Risks** | Some suites genuinely need host+renderer (grid pipeline, remote host). Keep an explicit `draxul-test-host-integration` rather than oversplitting. Coordinate with pending `08` label tooling. |

---

### 3. `draxul-runtime-support` mixes unrelated subsystems under one PUBLIC fan-out

| Field | Detail |
|---|---|
| **Location** | `libs/draxul-runtime-support/CMakeLists.txt` (sources + PUBLIC links: config, font, grid, host-identity, **nvim**, renderer, **protocol**, window); headers `grid_rendering_pipeline.h`, `ui_request_worker.h` (includes `nvim_rpc.h`), `pane_print.h`, `system_resource_monitor.h`, `cursor_blinker.h` |
| **Priority** | **P1** |
| **Current structural problem** | One archive owns grid→GPU pipeline, Nvim UI-request worker, print PDF, OS resource sampling, and cursor blink. PUBLIC nvim/protocol/window/renderer means any consumer of “runtime support” inherits the wide graph. That blocks narrow testing of print/crop math or resource sampling and couples host/grid work to nvim transport. |
| **Proposed boundary** | Split into at least: `draxul-grid-pipeline` (grid flush/ligature/atlas upload), `draxul-nvim-ui-worker` (or fold into nvim-transport after pending `03`), `draxul-pane-print` (crop/PDF/platform dialog), keep monitor/blink as tiny libs or under types/runtime. Compatibility INTERFACE `draxul-runtime-support` can re-export during migration. |
| **Dependency shape** | Grid pipeline: grid + font + renderer interfaces. UI worker: nvim channel only. Print: types + platform frameworks (private). Monitor: no GPU. |
| **Migration path** | Introduce new STATIC targets; retarget includes; make aggregate INTERFACE; update host and tests one consumer at a time. |
| **Testing improvement** | Print crop/PDF pure tests without nvim; grid pipeline tests without protocol; worker tests against `IRpcChannel` fakes only. |
| **Agent-work benefit** | Separate ownership for print, grid upload, and nvim worker; fewer merge collisions on one CMakeLists. |
| **Risks** | Host currently expects one “runtime support” bundle — keep the aggregate until consumers migrate. Windows print path is weaker than macOS; preserve platform stubs. |

---

### 4. Product shaders are root-owned and not option-gated on Vulkan

| Field | Detail |
|---|---|
| **Location** | `shaders/` (grid/gui/markdown + **all** megacity_* + satview_*); `cmake/CompileShaders.cmake` GLOB of every `*.vert`/`*.frag` (lines 11–31); Metal side creates product targets always (`CompileShaders_Metal.cmake`) but root only `add_dependencies` when options ON (`CMakeLists.txt` ~444–465). Staging of assets is gated; **Windows SPIR-V compile is not**. |
| **Priority** | **P1** |
| **Current structural problem** | Optional product shaders live in a shared root directory and compile into the default Windows shader target even when `DRAXUL_ENABLE_MEGACITY=OFF` / `SATVIEW=OFF`. Product and core shader work collide in one tree and one GLOB; OFF builds still pay compile cost and risk. |
| **Proposed boundary** | Per-product shader roots or explicit file lists: `shaders/core/`, `shaders/megacity/`, `shaders/satview/` (or module-local `modules/*/shaders/`) with CMake targets owned by each product subdirectory. |
| **Dependency shape** | Core `compile_shaders` → grid/gui/nanovg/markdown only. Product targets depend on their module CMake and optional gates. |
| **Migration path** | 1) Replace GLOB with explicit core list. 2) Gate product shader targets with the same options as libraries. 3) Move files in a later mechanical PR; update ABI parity tests paths. |
| **Testing improvement** | `DRAXUL_ENABLE_*=OFF` configure/build no longer compiles product shaders; clearer ownership for shader ABI tests. |
| **Agent-work benefit** | SatView/Megacity agents edit isolated shader dirs/targets without touching core grid GLSL/MSL. |
| **Risks** | Shared includes (`decoration_constants_shared.h`, `quad_offsets_shared.h`) need a small shared include dir; keep Metal/Vulkan parity when moving paths. |

---

### 5. Metal backend is a single TU; Vulkan is already multi-file

| Field | Detail |
|---|---|
| **Location** | `libs/draxul-renderer/src/metal/metal_renderer.mm` (~1128 lines) + `metal_renderer.h`; Vulkan: `vk_renderer.cpp`, `vk_context`, `vk_pipeline`, `vk_buffers`, `vk_atlas`, `vk_resource_helpers` (`draxul-renderer/CMakeLists.txt` ~63–94) |
| **Priority** | **P1** |
| **Current structural problem** | macOS grid/3D/ImGui/capture/atlas work concentrates in one ObjC++ TU. Vulkan already has private helpers and OBJECT libraries. Parallel Metal agents collide; reviews cannot scope “buffers only” vs “swapchain only.” |
| **Proposed boundary** | Mirror Vulkan privately: `metal_context`, `metal_pipeline`, `metal_buffers`, `metal_atlas`, keep `MetalRenderer` as façade implementing `IGridRenderer` / `IImGuiHost` / `ICaptureRenderer`. Keep headers under `src/metal/` (ObjC++-only, as today). |
| **Dependency shape** | Callers unchanged (`draxul-renderer` public API). Implementation-private TUs; no new public Metal types. |
| **Migration path** | Extract atlas upload and buffer allocation first (lowest coupling), then pipeline/depth/capture. One PR per subsystem; keep `create_metal_renderer()` factory. |
| **Testing improvement** | Existing white-box path via `draxul-renderer-test-internals`; unit-test pure CPU helpers if any emerge. |
| **Agent-work benefit** | File-level ownership parity with Vulkan; smaller review diffs on macOS. |
| **Risks** | ObjC++ ARC and layout of `MetalRenderer` members — keep single owning class for ObjC refs; extract free functions / nested helpers carefully. |

---

### 6. `draxul-scoreview` PUBLIC-links audio DSP into pure layout/learn consumers

| Field | Detail |
|---|---|
| **Location** | `modules/score/draxul-scoreview/CMakeLists.txt` lines 49–52: `PUBLIC … draxul-score-audio`; audio target pulls kissfft + tinysoundfont (`draxul-score-audio/CMakeLists.txt`). Host already owns mic/SDL (`draxul-scoreview-host`). Pending `07` only splits analysis overlay draw. |
| **Priority** | **P2** |
| **Current structural problem** | Core scoreview (Verovio layout, flow, timemap, SVG interpreter) transitively exposes audio to every `draxul-test-scoreview` consumer. Audio was isolated as a library, then re-widened via PUBLIC link “for host headers.” |
| **Proposed boundary** | Make `draxul-score-audio` **PRIVATE** to host (or a thin `draxul-score-audio-host` façade). Keep pure player-input events on `draxul-score-input`. If `flow_controller` only needs event types, depend on input, not audio. |
| **Dependency shape** | `draxul-scoreview`: types + learn + input (+ notation as needed). Host: scoreview + audio + nanovg + host. |
| **Migration path** | Audit public headers for audio includes; move synth includes to host-private headers; flip link visibility; rebuild `draxul-test-scoreview`. |
| **Testing improvement** | Layout/composer/analysis tests without soundfont/FFT link cost. |
| **Agent-work benefit** | Audio DSP agents vs engraving agents stop sharing one link edge. |
| **Risks** | Any remaining PUBLIC header that returns synth types must move or use opaque handles. |

---

### 7. Control CLI and weather are library-coupled app side features

| Field | Detail |
|---|---|
| **Location** | `app/control_cli.{h,cpp}` (~parse + run; used from `main.cpp` ~557–569); `app/weather_service.{h,cpp}` owned by `App` (`app.h` weather member); both compiled into `draxul-app` (`CMakeLists.txt` ~298–301); control parse tests in `draxul-test-app` |
| **Priority** | **P2** |
| **Current structural problem** | CLI argument parsing/dispatch and HTTP weather polling are not app-frame orchestration, yet they expand `draxul-app`’s link surface (`draxul-http`, control clients) and test classification. Agents touching CLI protocol or weather hit the same library as chrome and topology. |
| **Proposed boundary** | `draxul-control-cli` (parse + client invoke; depends on `draxul-control` / `draxul-client` only) linked from the executable/`main.cpp`; `draxul-weather` (or keep under http services) with `IHttpClient` injection — App only starts/stops it. |
| **Dependency shape** | Executable → control-cli. App → weather interface. Tests for parse link control-cli only. |
| **Migration path** | Move parse first (already pure-ish); then weather service; leave App wiring thin. |
| **Testing improvement** | Control CLI parse/run tests without full App; weather unit tests with fake HTTP only. |
| **Agent-work benefit** | Server/control protocol work and chrome/UI work diverge. |
| **Risks** | Low; keep session-id defaults and exit codes stable for scripts. |

---

### 8. `ServerKernel` is a multi-service façade in one large TU / single target

| Field | Detail |
|---|---|
| **Location** | `libs/draxul-server/src/server_kernel.cpp` (~3580+ lines); private collaborators already exist (`topology_service.*`, `remote_terminal_service.*`, `session_topology_bridge.*`, `server_terminal_runtime.*`, `server_agent_service.*`) but one `draxul-server` STATIC target (`libs/draxul-server/CMakeLists.txt`) |
| **Priority** | **P2** |
| **Current structural problem** | Services are file-split but not target-split. Kernel TU still owns lifecycle, control loop, checkpoint, eviction, agent mutations, and terminal registry wiring. Server-focused agents and tests always rebuild the whole archive; hard to test topology alone without the full kernel. |
| **Proposed boundary** | Optional STATIC internals: `draxul-server-topology`, `draxul-server-terminal`, `draxul-server-agent`, with `draxul-server` as kernel façade/public API (`server_kernel.h`, `server_agent_service.h`). |
| **Dependency shape** | Unchanged public headers for app/executable. Internals depend on protocol/control/session-model/terminal-process only (already enforced by `CheckDependencyBoundaries.cmake`). |
| **Migration path** | Extract pure service methods from kernel TU first; then CMake sub-targets; keep link isolation executable on the façade. |
| **Testing improvement** | Topology/agent unit tests link smaller archives; kernel integration tests remain. |
| **Agent-work benefit** | Clear ownership of terminal runtime vs topology vs agents. |
| **Risks** | Over-splitting micro-libraries — prefer 2–3 service targets, not one-per-file. Do not reintroduce host/window deps. |

---

### 9. Stale nested agent guidance still points at monolithic test binaries

| Field | Detail |
|---|---|
| **Location** | `modules/megacity/AGENTS.md` Validation section (~69–84): `draxul-tests.exe`, tags `[megacity]…`, missing files like `megacity_scene_tests.cpp`; root `Claude.md` still documents deleted host hierarchy (`I3DHost` / `attach_3d_renderer`, ~127–130). Full repair is **pending `08`** — listed here only as **sequencing risk**, not a new card. |
| **Priority** | **P1** (already tracked by `kanban/pending/08 agent-guidance-label-validation -refactor.md`) |
| **Note** | Do not implement a parallel guidance fix; land `08` after target renames from pending `02–07`. |

---

## Explicitly not re-reported (already tracked)

| Tracked item | Why skipped |
|---|---|
| Pending `00` internal-target policy | Sanitizer/coverage/`/FS` list problem |
| Pending `01` `app/CMakeLists.txt` | Root owns `draxul-app` sources |
| Pending `02` host API/grid/terminal splits | Broad `draxul-host` |
| Pending `03` nvim protocol/transport | Single `draxul-nvim` |
| Pending `04` kanban core/host | Host mixed into kanban target |
| Pending `05` megacity model library | `semantic_city_layout.cpp` ~2.2k CPU model |
| Pending `06` satview host/composer | `satview_host.cpp` ~5.3k |
| Pending `07` score overlay build/draw | NanoVG in core path |
| Pending `08` guidance + `do.py test --label` | Docs/tooling |
| Ice-box `22` app tab/session controllers | Remaining App topology projection bulk (~`App::apply_remote_topology_*`, ~3k–3.6k in `app.cpp`) |
| Ice-box `125` overlay registry | Overlay host hardwiring in App |

---

## Proposed module / target map (incremental, beyond pending)

```text
draxul (exe) ── main, product provider registration, assets/shaders stage
├── draxul-control-cli          [new, P2] parse/run Session control CLI
├── draxul-app                  orchestration, ChromeHost, PaneManager, topology adapter
│     └── draxul-app-shell      [new, P1] SplitTree, shell/chrome layout, fuzzy_match
├── draxul-runtime-support      [compat INTERFACE during split]
│     ├── draxul-grid-pipeline  [new, P1]
│     ├── draxul-pane-print     [new, P1]
│     └── draxul-nvim-ui-worker [new / fold into nvim-transport after pending 03]
├── draxul-host*                [pending 02]
├── draxul-nvim-protocol/transport [pending 03]
├── draxul-server               façade
│     ├── draxul-server-topology / -terminal / -agent  [optional P2]
├── products (existing + pending 04–07)
└── tests
    ├── draxul-test-foundation / terminal / nvim / renderer / server-client  [P1 wave 2]
    ├── draxul-test-app-shell (or core) for pure shell
    └── existing product/app targets
```

Shader map: `shaders/core/*` always; `shaders/megacity/*` and `shaders/satview/*` only when options ON.

---

## Best isolated work packages for parallel agents

| Package | Owner surface | Validate with |
|---|---|---|
| **A. App-shell library** | pure layout/split/fuzzy sources + reclass tests | build shell + pure tests only |
| **B. Runtime-support split** | grid pipeline / print / worker | host + existing pipeline/print tests |
| **C. Shader gating + ownership** | `CompileShaders*.cmake` + move lists | configure ON/OFF product flags both platforms |
| **D. Metal multi-TU** | `src/metal/*` only | macOS build + render smoke; leave Vulkan owner alone |
| **E. Scoreview audio link hygiene** | score CMake + header includes | `draxul-test-scoreview` / host labels |
| **F. Control-cli extract** | `control_cli.*` + main + parse tests | app/control tests without full chrome |
| **G. Core test domain split** | `tests/CMakeLists.txt` classification | partition baseline + focused ctest labels |
| **H. Pending sequence 00→08** | already assigned | do not parallelize against A–G on same CMake roots without freeze |

Avoid parallel edits to root `CMakeLists.txt`, `tests/CMakeLists.txt`, and `app/app.cpp` without a single owner.

---

## Strong existing structural qualities (preserve)

- **Foundation boundary enforcement** via `cmake/CheckDependencyBoundaries.cmake` (client/server/protocol/terminal free of window/renderer/host).
- **Product option gates** and executable-only product registration (`draxul-app` deliberately not linking megacity/satview/score).
- **Markdown core/host**, **SatView multi-library**, **Score learn/core/host**, **Megacity geometry/semantics/scene/renderer** as working split patterns.
- **`TopologyProjection`** already renderer-free in `draxul-client` — App is only the adapter (further extraction is ice-box `22`, not greenfield).
- **Modular test executables + exact partition/tag baselines** and `*-test-internals` / link-isolation targets.
- **`AppDeps` / fakes** for window/renderer/host injection without rewriting production types.
- **Vulkan private resource helpers** and public `IRenderPass` / `IRenderContext` hierarchy — good model for Metal parity.
- **Renderer-free terminal-core + server-owned PTY** direction already in module map and host CMake (`draxul-host` rejects `draxul-terminal-process`).

---

## Recommended sequence (new work only)

1. **App-shell pure library** (quick win, unblocks pure tests).  
2. **Vulkan shader option-gating** (small CMake change, big isolation).  
3. **Runtime-support split** (dependency leverage).  
4. **Metal multi-TU** (parallel with 3 if different owners).  
5. **Core test domain split** (after pending host/nvim renames if possible).  
6. **Score audio PUBLIC→PRIVATE**, control-cli/weather extraction, optional server service targets.

Land pending `00` before adding many new CMake targets; land pending `08` after renames so docs name the final graph.
