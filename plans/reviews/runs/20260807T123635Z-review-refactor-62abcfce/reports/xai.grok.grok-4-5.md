I'll inspect the repository structure, CMake targets, agent guides, and kanban items to produce a refactoring-focused review only.Next I'll read the pending refactor kanban items and inspect large hotspots so findings don't duplicate existing work.# Refactor Review — Code Hygiene & Agent Isolation

Scope: structural hygiene only. Pending cards `00`–`08` already own host/Nvim/Kanban/Megacity-model/SatView-composer/Score-overlay/CMake-policy/guidance work and are **not** restated as new findings. Ice-box `22` (session projection) and `125` (overlay registry) remain related follow-ons.

---

## Findings (by leverage)

### 1. `draxul-runtime-support` is a grab-bag that PUBLIC-links Neovim

| | |
|---|---|
| **Priority** | **P1** |
| **Location** | `libs/draxul-runtime-support/CMakeLists.txt` L1–22; `include/draxul/ui_request_worker.h` L4 (`#include <draxul/nvim_rpc.h>`); co-located TUs: `grid_rendering_pipeline.*`, `cursor_blinker.*`, `pane_print*`, `system_resource_monitor.*`, `ui_request_worker.*`. Consumer: `libs/draxul-host/include/draxul/grid_host_base.h` L6 includes `grid_rendering_pipeline.h`. Host CMake PUBLIC-links `draxul-runtime-support` (`libs/draxul-host/CMakeLists.txt` L17–26). |
| **Current structural problem** | One target mixes (a) grid→GPU flush/ligature expansion, (b) cursor blink timing, (c) platform print PDF, (d) OS resource sampling, (e) Nvim resize RPC worker. Because `UiRequestWorker` needs `IRpcChannel`, **`draxul-nvim` is PUBLIC** on the whole archive. Any consumer of the grid pipeline (all `GridHostBase` hosts, including product hosts and tests) inherits Nvim/msgpack compile+link weight even when they never touch RPC. This undercuts pending host-API narrowing. |
| **Proposed boundary** | Split into narrow static libs: `draxul-grid-pipeline` (pipeline + blinker; deps: grid/font/renderer/types), `draxul-system-monitor` (types only), `draxul-pane-print` (window/renderer + platform private), `draxul-nvim-ui-worker` (or fold into post-`03` `draxul-nvim-transport`; deps: nvim protocol channel only). Delete the umbrella once callers migrate. |
| **Dependency shape** | Callers: `GridHostBase` → pipeline only; `NvimHost` → pipeline + worker; `App` print path → pane-print; diagnostics → monitor. Public API stays existing headers under `include/draxul/`. Platform print `.mm` stays PRIVATE. |
| **Migration path** | 1) Add `draxul-grid-pipeline` with current pipeline/blinker sources; retarget `GridHostBase` links. 2) Move worker with Nvim link only. 3) Move print/monitor. 4) Keep empty `draxul-runtime-support` INTERFACE aggregate briefly. 5) Drop aggregate. |
| **Testing improvement** | Pipeline/ligature tests link pipeline-only; worker tests use fake `IRpcChannel` without renderer/print; print tests stay platform-gated. |
| **Agent-work benefit** | Nvim transport agents stop colliding with grid/print/monitor edits; product hosts rebuild without nvim when only pipeline changes. |
| **Risks** | Coordinate with pending `02`/`03` so worker lands on final Nvim target names. Windows/macOS print paths must stay private to pane-print. |

---

### 2. `draxul-app` is still a multi-domain archive (~5.5k-line `App` + chrome/control/CLI)

| | |
|---|---|
| **Priority** | **P1** |
| **Location** | Root `CMakeLists.txt` L281–314 (`draxul-app` sources); `app/app.h` L87–373 (state surface); `app/app.cpp` ~5486 lines (remote topology apply, session checkpoint, control mutations ~L5044+, frame loop); chrome cluster `chrome_{host,layout,text_layer,vector_pass,pill}.*`, `rename_editor.*`; partial router `control_request_router.*` (reads only, ends at L274–275 with `method_not_found`); mutations remain in `App::handle_control_request`. |
| **Current structural problem** | Module map says `app/` is orchestration only (`docs/module-map.md` L58–77), but `draxul-app` still owns reusable chrome layout/render, pure fuzzy matching, weather HTTP, control CLI/journal, and large control mutation logic. Tab collection is already in `TabController` (`app/tab_controller.h`), so ice-box `22` is partially landed; residual growth is topology/session projection + control mutations + chrome. Any chrome or control edit rebuilds the full app archive and collides with frame-loop work. |
| **Proposed boundary** | After pending `01` (`app/CMakeLists.txt`): (1) **`draxul-chrome`** static lib — layout/rename/text/vector/pill/host assembly; deps: gui, nanovg, font, config theme, session-model tab views via narrow callbacks. (2) **`ControlMutationRouter`** (or expand `ControlRequestRouter`) — space/agent focus/start/restart/send/wait with injected ops (`activate_space`, `launch_agent`, pane lookup), not `App` methods. (3) Leave `App` with window/renderer/text lifecycle, pump, and adapters. Session projection remains ice-box `22`. |
| **Dependency shape** | `draxul-app` → `draxul-chrome` + controllers; chrome must not include `app.h`. Control router depends on `SpaceController`/`AgentController` interfaces only. |
| **Migration path** | Mechanical CMake move of chrome sources → link; port chrome tests to `draxul-test-chrome` or keep app tests linking chrome; move mutation arms from `App::handle_control_request` one method family at a time with existing control tests. |
| **Testing improvement** | Chrome layout/rename already unit-testable without full remote topology; control mutation tests can drive router with fake ops without GPU. |
| **Agent-work benefit** | Parallel lanes: chrome visuals vs topology/session vs control plane vs frame loop. |
| **Risks** | Chrome hits `SpaceController`/`PaneManager` today — introduce view/callback seams carefully. Do not mix with pending `01` in the same change. |

---

### 3. Focused test targets still force oversized link closures

| | |
|---|---|
| **Priority** | **P1** |
| **Location** | `tests/CMakeLists.txt` L247–268 (`draxul-test-core` links `draxul-host`, `draxul-server`, `draxul-nanovg`, `draxul-client`, `draxul-protocol`, renderer, …); L274–292 (`draxul-test-app` links **all** optional product hosts when enabled). Classification is good (L16–128), but default residual core still absorbs anything unclassified. |
| **Current structural problem** | Modular executables exist (`done/35`), yet “core” is not core: font/grid/VT/protocol tests pay host+server+NanoVG. App tests pull Megacity/SatView/ScoreView for provider registration (`host_provider_availability_tests.cpp`), so optional-product agents force full product link when touching chrome/CLI/session. |
| **Proposed boundary** | Further split: `draxul-test-terminal` (host presentation + terminal-core fixtures), `draxul-test-server` (kernel/protocol/client without window), keep `draxul-test-core` for types/font/grid/config/window fakes only. For app: `host_provider_availability_tests` should use **fake registry registration**, not real product host libs; drop optional product links from `draxul-test-app` except a tiny `draxul-test-app-products` smoke target. |
| **Dependency shape** | Each executable PUBLIC/PRIVATE only its owner libs; product test targets remain option-gated. |
| **Migration path** | Reclassify sources in `tests/CMakeLists.txt`; fix link failures by moving tests or adding fakes; preserve partition/baseline checks (L113–199). |
| **Testing improvement** | `ctest -L core` becomes the cheap agent default; product OFF configs stop rebuilding app tests against disabled hosts. |
| **Agent-work benefit** | Smaller link/edit scope; fewer merge conflicts on `tests/CMakeLists.txt` product branches. |
| **Risks** | Some “core” suites truly need host fixtures — keep an explicit terminal label, not silent re-bloat of core. |

---

### 4. Metal grid backend is a single large OBJECT; Vulkan is already modular

| | |
|---|---|
| **Priority** | **P1** |
| **Location** | `libs/draxul-renderer/CMakeLists.txt` L25–50 (`draxul-renderer-metal` = one `metal_renderer.mm` ~1132 lines); L60–104 Vulkan: `draxul-vulkan-resources` + `vk_{context,pipeline,buffers,atlas,cube_pass,renderer}.cpp`. Shared contract: `src/shared/grid_contract.h`. |
| **Current structural problem** | Backend work is asymmetric: Vulkan agents can own atlas/buffers/pipeline files; Metal agents always edit one TU, raising conflict cost and making parity refactors harder to review. Ice-box `25` is feature/parity cleanup, not this file-boundary issue. |
| **Proposed boundary** | Mirror Vulkan privately: `metal_context`, `metal_pipeline`, `metal_buffers`, `metal_atlas`, `metal_frame` under `src/metal/`, still one OBJECT target `draxul-renderer-metal`. Keep public API in `include/draxul/` only. |
| **Dependency shape** | Unchanged public `draxul-renderer`; private Metal files only. |
| **Migration path** | Extract non-draw helpers first (resource create/destroy, atlas upload, capture), then frame/pass; keep one compiling OBJECT throughout. |
| **Testing improvement** | Existing renderer-state / capture / render-snapshot tests; optional smaller unit hooks for atlas sizing if extracted pure. |
| **Agent-work benefit** | Parallel Metal vs Vulkan file ownership; clearer ABI-parity review diffs. |
| **Risks** | ObjC++ ARC lifetime across files; do not invent a shared C++ GPU abstraction (done `28` already chose private helpers). |

---

### 5. Compatibility shim re-couples session codec to `PaneManager`

| | |
|---|---|
| **Priority** | **P2** |
| **Location** | `app/session_state.h` L1–9 (`#include <draxul/session_state.h>` then `#include "pane_manager.h"`); included from `app.h` L11, `tab_controller.h` L3. Real codec lives in `libs/draxul-session-model/`. |
| **Current structural problem** | Any TU that wants session path helpers via the app shim also pulls host lifecycle/`PaneManager`/`SplitTree`. That undoes the session-model extraction for app-local includes and confuses agents about ownership. |
| **Proposed boundary** | Make `app/session_state.h` a pure re-export of `draxul/session_state.h` only. Callers that need panes include `pane_manager.h` / `tab.h` explicitly. |
| **Dependency shape** | Session I/O: session-model only. UI restore: app controllers. |
| **Migration path** | Grep app includes; fix compile breaks by adding direct includes; delete pane include from shim. |
| **Testing improvement** | None required beyond existing session_state tests. |
| **Agent-work benefit** | Clear “codec vs live UI” boundary in include graph. |
| **Risks** | Low; mechanical include fixups. |

---

### 6. Root `shaders/` mixes core and optional product assets

| | |
|---|---|
| **Priority** | **P2** |
| **Location** | `shaders/` — core `grid_*`, `gui_*`, `nanovg_*`, `markdown_*` alongside `megacity_*`, `satview_*`; compile/stage wiring in root `CMakeLists.txt` (Metal stage helpers ~L421+) and `cmake/CompileShaders*.cmake`. |
| **Current structural problem** | Optional-product shader edits share one directory and root CMake staging with core grid shaders. Agents touching SatView post collide with grid/markdown staging ownership; OFF builds still require careful gating. |
| **Proposed boundary** | Keep core shaders at repo root (or `shaders/core/`). Move product GLSL/MSL next to owning module (`modules/satview/shaders/`, `modules/megacity/shaders/`) with module CMake owning compile/stage when option ON. |
| **Dependency shape** | Product renderers depend on module shader targets; core renderer never lists product shaders. |
| **Migration path** | Move files + update compile scripts one product family; keep output path `build/.../shaders/` for runtime compatibility. |
| **Testing improvement** | Configure ON/OFF; existing host launch / shader ABI tests. |
| **Agent-work benefit** | Product agents own shader tree without root CMake conflicts. |
| **Risks** | Install/bundle copy paths; both Vulkan glslc and Metal xcrun lists must move together. |

---

### 7. `main.cpp` mixes process roles (client UI, shared server, CLI control)

| | |
|---|---|
| **Priority** | **P2** |
| **Location** | `app/main.cpp` (~1140+ lines): host registration, shared-server bootstrap, remote host factory (L991–1011), smoke/screenshot/render-test modes, control CLI. Executable target still only `app/main.cpp` (`CMakeLists.txt` L349–352). |
| **Current structural problem** | One TU is the collision hub for server lifecycle, product provider registration, and client launch. Hard to give “server entry” vs “client entry” exclusive ownership. |
| **Proposed boundary** | `app/client_main.cpp` (UI App run paths), `app/server_entry.cpp` (kernel/singleton helpers already used), thin `main.cpp` dispatch on parsed mode; keep single `draxul` binary. |
| **Dependency shape** | Server entry links `draxul-server` only for server mode paths; client path stays `draxul-app` + optional hosts. |
| **Migration path** | Extract functions without behavior change; preserve macOS helper bundle copy rules. |
| **Testing improvement** | Existing server kernel / control / smoke paths; no new runtime surface. |
| **Agent-work benefit** | Server agents avoid UI host-registration churn. |
| **Risks** | Signal handling and Win32 console/`WIN32_EXECUTABLE` setup must stay consistent across files. |

---

### 8. `draxul-app-support` is an empty INTERFACE that misleads ownership

| | |
|---|---|
| **Priority** | **P2** |
| **Location** | `libs/draxul-app-support/CMakeLists.txt` (INTERFACE → config + runtime-support + render-test); `docs/module-map.md` L214; many ice-box cards still cite `libs/draxul-app-support/src/*` for split_tree/input/config (paths are stale — those live under `app/` or other libs). |
| **Current structural problem** | Named like a real library, used by `draxul-test-core`, but holds no sources. Agents following ice-box paths waste time; the target hides three real deps behind a fake boundary. |
| **Proposed boundary** | Either (a) delete and link the three deps explicitly, or (b) fill it only after chrome/CLI pure pieces move out of `app/` (finding 2). Prefer (a) until real sources exist. |
| **Dependency shape** | Callers name `draxul-config` / `draxul-runtime-support` (post-split) / `draxul-render-test` directly. |
| **Migration path** | Update `tests/CMakeLists.txt` and module-map; leave ice-box path fixes to pending `08` guidance pass. |
| **Testing improvement** | Link isolation unchanged. |
| **Agent-work benefit** | Stops false “edit app-support” routing. |
| **Risks** | None if purely CMake/docs. |

---

### 9. `draxul-scoreview` PUBLIC-links audio and input device stacks

| | |
|---|---|
| **Priority** | **P2** |
| **Location** | `modules/score/draxul-scoreview/CMakeLists.txt` L45–52 (`PUBLIC draxul-score-learn draxul-score-input draxul-score-audio`); comments note header includes force PUBLIC. Pending `07` only splits analysis-overlay draw. |
| **Current structural problem** | Layout/timemap/flow consumers inherit MIDI/synth linkage because headers leak device types. Core Score tests that only need document/flow still see audio/input closure. |
| **Proposed boundary** | Keep `draxul-scoreview` for layout/judge/timemap; move device-facing types behind host or `draxul-score-runtime` that PUBLIC-links input/audio. Prefer forward declarations / value DTOs in public flow headers. |
| **Dependency shape** | learn (GPU-free) ← scoreview (layout) ← host (nvg + devices). |
| **Migration path** | Inventory includes from `flow_controller.h` / public headers; break device includes; flip CMake to PRIVATE where possible; re-run `draxul-test-scoreview`. |
| **Testing improvement** | Core scoreview label stays free of SDL/MIDI where tests don't need them. |
| **Agent-work benefit** | Learning/layout agents avoid audio device rebuilds. |
| **Risks** | Don't break host audio rig wiring; macOS TCC path stays host-only. |

---

### 10. Pure `fuzzy_match` still ships inside `draxul-app`

| | |
|---|---|
| **Priority** | **P2** |
| **Location** | `app/fuzzy_match.{h,cpp}`; listed in root `CMakeLists.txt` L303; tests `tests/fuzzy_match_tests.cpp` (app test target). |
| **Current structural problem** | Stable, dependency-free scoring algorithm is compiled into the full app archive and tested only via `draxul-test-app`. |
| **Proposed boundary** | `draxul-fuzzy` (header + one cpp, types only) or fold into `draxul-gui` if palette-only. |
| **Dependency shape** | Command palette / any future picker → fuzzy; no SDL/GPU. |
| **Migration path** | Move files + CMake; reclassify test to core. |
| **Testing improvement** | Fuzzy tests on `draxul-test-core` without app/host. |
| **Agent-work benefit** | Tiny isolated target; no app rebuild for scoring tweaks. |
| **Risks** | None. |

---

## Already tracked (do not re-open as new work)

| Card | Owns |
|------|------|
| pending `00` | Sanitizer/coverage/`/FS` target-local policy |
| pending `01` | `app/CMakeLists.txt` for `draxul-app` |
| pending `02` | Host API / grid-host / terminal presentation / Nvim host splits |
| pending `03` | Nvim protocol vs transport |
| pending `04` | Kanban core vs host |
| pending `05` | Megacity model/layout/routing lib (`semantic_city_layout.cpp` ~2.2k still in product target) |
| pending `06` | SatView scene composer + host panel split (`satview_host.cpp` still multi-kLOC) |
| pending `07` | Score analysis-overlay build vs NanoVG draw |
| pending `08` | Guidance/label validation (incl. stale `draxul-tests.exe` in `modules/megacity/AGENTS.md` L69–74) |
| ice-box `22` | Session projection extraction from `App` (TabController already exists) |
| ice-box `125` | Overlay registry for chrome/palette/toast/diagnostics |

---

## Proposed module/target map (additive)

```text
draxul (exe)
├── main dispatch (client_main / server_entry)     [finding 7]
├── draxul-app          orchestration only
│     ├── draxul-chrome [finding 2]
│     ├── control routers (read + mutation) [finding 2]
│     └── space/tab/pane controllers
├── draxul-host-api / draxul-grid-host / …   [pending 02]
├── draxul-grid-pipeline / pane-print / monitor / nvim-ui-worker  [finding 1]
├── draxul-nvim-protocol / draxul-nvim-transport [pending 03]
├── draxul-renderer (metal_* split private) [finding 4]
└── product modules (existing + pending 04–07)

tests:
  core | terminal | server | app | chrome | markdown/kanban | megacity | satview | scoreview | scoreview-host
```

---

## Best parallel agent packages (after `00` freezes policy)

1. **Runtime-support split** (finding 1) — unblocks host/product narrow links; coordinate names with `02`/`03`.
2. **Chrome static library** (finding 2) — after `01`; exclusive `chrome_*` + tests.
3. **Control mutation router** (finding 2) — exclusive `app.cpp` control region / new router TU.
4. **Metal backend file split** (finding 4) — exclusive `src/metal/*`; no product modules.
5. **Test link narrowing** (finding 3) — exclusive `tests/CMakeLists.txt` + fakes.
6. **Session shim include fix** (finding 5) — tiny, parallel anytime.
7. **Shader tree move per product** (finding 6) — one agent per product family.
8. Land **pending `05`/`06`/`07`** as already planned product-internal packages.

Avoid parallel edits to root CMake until `00`/`01` complete.

---

## Strong existing qualities to preserve

- **Foundation isolation + configure checks**: `draxul-terminal-core` / `client` / `server` / `session-model` / `protocol` with `cmake/CheckDependencyBoundaries.cmake`.
- **Executable-only product registration**: `draxul-app` deliberately does not link Megacity/SatView/ScoreView (`CMakeLists.txt` L342–344, L381–391).
- **Markdown core vs host precedent**: `draxul-markdown` + `draxul-markdown-host` is the template for Kanban (`04`) and chrome extraction.
- **Modular test partition with static inventory guards**: `tests/CMakeLists.txt` partition/baseline/tag fingerprinting.
- **Link-isolation executables**: `tests/link_isolation/*` for public-header contracts.
- **Vulkan private resource target**: `draxul-vulkan-resources` + product migration (done `28`) without public backend leakage.
- **Renderer-free session/topology stack**: client/server free of window/renderer/host (enforced).
- **AppDeps / fake hosts / replay fixtures**: real unit seams already exist — extend them rather than inventing new frameworks.

---

*Review-only; no files modified.*
