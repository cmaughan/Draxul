All three module agents have reported. Combining their findings with my own analysis of the app layer, core libraries, build system, and test architecture, here is the complete review.

---

# Draxul — Structural / Refactoring Review

Focus: code hygiene, modularity, testability, and safe parallel-agent work. Runtime correctness and features are out of scope except where structure is the reason code cannot be isolated or tested.

## Executive summary

The repository is **structurally healthy at the target/boundary level** and unhealthy at the **translation-unit level**. The library graph is layered, downward-linking, and enforced (`cmake/CheckDependencyBoundaries.cmake`, per-target `*-test-internals` INTERFACE seams, link-isolation tests). Backend types do not leak through public headers. The debt is concentrated in a handful of **god translation units** — one per product host plus the shared renderers — and in a few **placement smells** where lower-layer logic (geometry, parsing, scene-building) has drifted up into host/app code.

The single highest-leverage pattern, repeated in all three optional product modules and in `app/`, is: **a 2,000–5,300-line host/`.cpp` that fuses lifecycle + input + picking + ImGui panels + scene assembly + config mapping behind one class with 40–100 members.** These are the primary agent-collision hotspots. The good news is the fix is uniform and low-risk: they all already have the `*-test-internals` seam and a partial controller-extraction precedent to copy.

Ranking is by leverage and sequencing, not raw file size.

---

## P0 — Blocks safe change

### P0-1 · `app/` orchestration: `App` is a god object, and workspace ownership is split with `ChromeHost`

**Location:** `app/app.h:70-236` (class `App`, ~60 methods, ~45 members); `app/app.cpp:2312-2544` (workspace management, ~250 lines) and `app/app.cpp:2035-2311` (session snapshot/persist/checkpoint/restore, ~280 lines); `app/chrome_host.h:24-60`.

**Structural problem.** `App` mixes: init/config-reload, text-service+font metrics, chrome-host wiring, GUI-action wiring, input dispatch, render-frame, print pipeline, diagnostics, **workspace lifecycle** (13 methods: `create_initial_workspace`/`add_workspace`/`close_workspace`/`activate`/`next`/`prev`/`move`/`by-index`/`recompute_all_viewports`/`find`/`require_active`), and **session persistence** (7 methods). Worse, there is a genuine **ownership contradiction**: `ChromeHost`'s own doc-comment (`chrome_host.h:24`) says *"ChromeHost is the central layout manager. It owns one or more Workspaces,"* but `App` actually owns `workspaces_` (`app.h:225`) and mutates it, while `ChromeHost` reads it back through a raw `const std::vector<std::unique_ptr<Workspace>>*` plus a fat bundle of `std::function` callbacks (`set_workspace_name`, `set_pane_name`, `get_pane_name`, `chord_indicator`, weather…) in `ChromeHost::Deps`. The header comment "moved from ChromeHost" (`app.h:153`) confirms this is mid-refactor residue. This bidirectional coupling means no agent can safely reason about workspace lifetime from one file.

**Proposed boundary.**
- Extract **`WorkspaceManager`** (new `app/workspace_manager.{h,cpp}`) owning `workspaces_`, `active_workspace_`, `next_workspace_id_`, and the 13 lifecycle/viewport methods. `App` holds one, and `ChromeHost` consumes its **read interface** (`workspaces()`, `active_id()`, `set_workspace_name(...)`) instead of raw pointers + callbacks.
- Extract **`SessionCoordinator`** (new `app/session_coordinator.{h,cpp}`) owning snapshot/persist/checkpoint/restore, composing the already-separate `session_state.cpp`/`session_cli.cpp`/`session_id.cpp`.

**Dependency shape.** Both are `draxul-app`-internal. Callers: `App` only. Public API: narrow read/command surfaces. `ChromeHost::Deps` shrinks by ~6 callbacks. No change to product modules.

**Migration path.** (1) Move the workspace methods verbatim into `WorkspaceManager`, `App` delegates — behavior identical, covered by existing `tests/active_workspace_invariant_tests.cpp`, `tests/host_manager_tests.cpp`, `tests/split_tree_tests.cpp`. (2) Repoint `ChromeHost` at the read interface, delete the raw pointer + name callbacks. (3) Extract `SessionCoordinator`, covered by `tests/session_state_tests.cpp`, `tests/startup_resize_state_tests.cpp`.

**Testing improvement.** Workspace invariants (active-id transitions on close, viewport recompute) become testable against `WorkspaceManager` without standing up window/renderer. Session round-trip already has tests that would move to target the coordinator directly.

**Agent-work benefit.** "Session persistence" and "tab/workspace behavior" become disjoint files instead of two regions of a 2,606-line `app.cpp` that every app task also edits.

**Risks.** None platform-specific; `app.cpp` is backend-agnostic. Main risk is the `IHostCallbacks`/focus-transition timing during `close_workspace` (`app.cpp:2360-2371`) — preserve the "activate replacement while both alive" ordering during the move.

---

## P1 — High-leverage hygiene

### P1-1 · Shared renderer god-objects with hand-maintained Vulkan/Metal parity (megacity + satview)

**Location:** `modules/megacity/draxul-codeviz-renderer/src/codeviz_render_vk.cpp` (4,432 lines, `CodeVizScenePass::State` at :233) and `codeviz_render.mm` (1,998, same struct at :177); `modules/satview/draxul-satview-renderer/src/satview_render_vk.cpp` (2,915, `State` at :530) and `satview_render.mm` (1,720, `State` at :20).

**Structural problem.** Each backend is a single `struct State` holding ~25–40 raw GPU handles and ~30 methods fusing: pipeline factory, resource residency/retirement rings, G-buffer/shadow/SSAO target management, material/label-atlas streaming, tooltip/overlay compositing, and frame recording — **plus ImGui debug UI inside the render backend** (`codeviz_render_vk.cpp:4301`, `.mm:1923`). The "list of render features" is encoded three times with no shared manifest: as scene-pass members/setters, as the Vulkan `State`'s ~25 named pipeline fields, and as the Metal `State`'s parallel set (satview: `_vk.cpp:571-592` vs `.mm:36-57`). **Nothing but matching identifier names and manual discipline keeps the two backends at parity** — the module's single biggest parity risk.

**Proposed boundary.**
- Internal (private `src/`) composable helpers mirrored on both backends: `ResourceRetirement` (deferred-free ring — pure bookkeeping, unit-testable), `GBufferTargets`/`ShadowResources`, `PipelineFactory`, `LabelAtlasResource`, `TooltipOverlayResource`. `State` becomes a thin orchestrator driving `record`.
- A **backend-neutral pipeline manifest** in the `scene` library: `enum class PipelineId` + a `constexpr {id, vert, frag}` table. Both backends hold `std::array<Pipeline, kCount>` indexed by the enum and iterate the table for create/destroy. A missing pipeline becomes a **compile/lookup error, not a silent parity gap.** The manifest names shaders as strings, so no GPU type leaks into `scene`.
- Move the ImGui debug UI out of the frame-recording TU (`codeviz_gbuffer_debug.cpp` per backend).

**Dependency shape.** Helpers stay renderer-private; manifest is public in `scene` but backend-neutral. `mesh_library.*`/`codeviz_material_library.*` already prove this compositional style works.

**Migration path.** Extract the retirement ring first (cheapest, immediately testable via the existing `*-renderer-test-internals`), then atlas/tooltip resources, then the pipeline manifest, then the factory last (highest churn). **Do each as paired Vulkan+Metal commits** per `modules/megacity/AGENTS.md` §Rendering.

**Testing improvement.** Retirement-ring and atlas-revision logic become device-free unit tests; a CI check can assert every `PipelineId` has a shader present on both platforms — parity becomes machine-verifiable instead of eyeballed.

**Agent-work benefit.** "Fix the shadow cascade" / "adjust the tooltip overlay" / "add an overlay" each touch a small file or a table entry instead of a 4,432-line TU shared with unrelated pipeline state.

**Risks (high).** Vulkan/Metal must move together or the gap must be reported. `GeometryMesh` 16-bit index invariant must be preserved for any relocated generator (megacity).

### P1-2 · Product host god-TUs: `satview_host.cpp` (5,317), `megacity_host.cpp` (2,418), `score_host.cpp` (2,186)

**Location:** `modules/satview/draxul-satview-host/src/satview_host.cpp` (~100 members, ~60 methods); `modules/megacity/draxul-megacity/src/megacity_host.cpp` (~90 members, ~40 methods); `modules/score/draxul-scoreview/src/score_host.cpp`.

**Structural problem.** Each fuses 6–8 separable concerns in one class/TU:

| Concern | satview | megacity | score |
|---|---|---|---|
| Scene/geometry builders | `append_*`/`*_color`, ~1,050 lines (`:197-998`, `:2435-2698`) | `build_procedural_*_mesh` in `city_builder.cpp` | — |
| Background-thread services | simulation-worker seam (clean) | **route worker + grid-build threads** fused (`:592-727`, `:1431`) | async-engrave (already in `ScoreStreamController` ✓) |
| Picking/selection | `select_nearest_*`, ~350 lines (`:4937-5174`) | `handle_click`/`handle_double_click` + hover FSM (`:1965-2261`) | — |
| ImGui panels | `render_control_panel` **816 lines** (`:3943-4759`) | `render_host_imgui` (`:1083`) | launch-token parse + panels |
| `draw()`/frame policy | ~486 lines (`:1283-1769`) | — | stream/window orchestration (`:595-1083`) |
| Config mapping | smeared across 3 files (see P2-3) | ImGui reconciliation inline | `current_config`/`apply_config` |

Megacity additionally publishes `RouteBuildRequest`/`RouteBuildResult`/`RetiredGridThread` in its **public** header (`megacity_host.h`) though nothing outside the module uses them.

**Proposed boundary (uniform across all three).** Follow the pattern score has already started (`ScoreAudioController`/`ScoreSessionController`/`ScoreStreamController`/`ScorePresentation`):
- **Move scene/geometry builders *down*** — satview `append_*` → `draxul-satview-scene`; megacity `build_procedural_*_mesh` → `draxul-geometry` (they already emit neutral POD / `GeometryMesh`).
- **Extract background-thread services** — megacity `RouteBuildService` + `GridBuildService` (owning the mutex/CV/cancellation-generation protocol, removing the structs from the public header).
- **Extract picking/selection** as free functions over `(snapshot, camera, viewport)` — pure, device-free.
- **Split ImGui panels** into per-section files editing a `*ControlPanelModel` value struct, with a `diff→side-effects` seam (satview already diffs `config_before = current_config()` at `:3946`).

**Dependency shape.** All host-internal except the downward builder moves. Public host headers **shrink** (megacity's especially). Consumed via the existing `*-host-test-internals` INTERFACE libs.

**Migration path.** Step 1 is always a **mechanical peer-`.cpp` split** (add files to the existing `add_library`) — zero API change, immediate parallel-edit relief and faster incremental builds. Then lift builders down, then introduce plain-struct models. Covered by existing `megacity_scene_tests`, `satview_*` suites, `scoreview_host_orchestration_tests`.

**Testing improvement.** Picking, scene assembly, panel side-effect dispatch, and the threading cancellation protocols become unit-testable without a GPU/window — today they are reachable only through `*HostTestAccess` friend hacks (evidence the responsibilities aren't factored).

**Agent-work benefit.** The largest single win in the repo: 6–8 concerns → 6–8 files, so parallel agents editing "picking" vs "the control panel" stop colliding in one multi-thousand-line hub.

**Risks.** Megacity threading extraction must preserve deterministic-shutdown ordering (`AGENTS.md` §Threading). Score's mic-permission ordering (SDL must not trigger the TCC dialog) must be preserved. No Vulkan/Metal risk for the host-side moves.

### P1-3 · `semantic_city_layout.cpp` (2,217) collapses the model→layout→grid→routing boundary the module's own guide mandates

**Location:** `modules/megacity/draxul-megacity/src/semantic_city_layout.{h,cpp}`.

**Structural problem.** `modules/megacity/AGENTS.md` explicitly says *"Preserve the boundary between semantic model construction and spatial layout,"* yet one header/TU hosts model construction, lot-packing layout, occupancy-grid (`CityGrid`, with a comment admitting it straddles layers), and pathfinding/route synthesis (`find_grid_path:962`, `build_city_routes_from_grid:1075`). A consumer needing only the neutral model transitively pulls in routing declarations.

**Proposed boundary.** Split into `semantic_city_model` → `semantic_city_layout` → `city_grid` → `city_routing`, strict one-way, all `draxul-megacity`-private. Header split first (source-compat include), then TU split by function group.

**Testing / agent benefit.** `megacity_scene_tests` can target `find_grid_path` against a hand-built `CityGrid` without running model construction; routing edits stop recompiling the whole model+layout TU. Pure functions → low-risk moves, no parity risk.

### P1-4 · Build wiring: no `app/CMakeLists.txt` — app sources are inline in the 611-line root `CMakeLists.txt`

**Location:** `CMakeLists.txt:268-292` (the `draxul-app` source list), which sits in the same file as options, sanitizer/coverage setup, all `add_subdirectory`, module gates, asset-copy, and executable wiring.

**Structural problem.** Every agent adding an `app/` source must edit the root CMake — the same file touched for every module gate, dependency edge, and shader/asset rule. This is a guaranteed merge-collision hotspot and forces app-scoped tasks to load 611 lines of unrelated build logic. Every other library and module already has its own `CMakeLists.txt`; `app/` is the sole exception.

**Proposed boundary.** Create `app/CMakeLists.txt` defining `draxul-app` (sources, includes, links) and reduce the root to `add_subdirectory(app)` + the executable/asset wiring. 

**Migration path.** Pure move of lines 268-321 into `app/CMakeLists.txt`; verify `cmake --preset default` and `-DDRAXUL_ENABLE_MEGACITY=OFF` both still configure. No source change.

**Agent-work benefit.** App-source edits stop colliding with module/option/executable edits; app tasks get a scoped build file.

---

## P2 — Useful cleanup

### P2-1 · Reusable pure parsers trapped inside domain TUs

- **`satview_catalog.cpp` (1,574):** an embedded `JsonReader`+UTF-8 emitter (`:100-516`, ~415 lines) and a CSV parser (`:721-855`) welded to catalog domain logic. Extract `satview_json.{h,cpp}` / `satview_csv.{h,cpp}` as `core`-private utilities (first check for an existing JSON facility under `draxul-types`). Enables fuzz/edge-case tests; no parity risk.
- **`svg_score_interpreter.cpp` (874, score):** a generic SVG path/transform parser (`parse_svg_path_data:216`, `parse_transform:52`) buried in the Verovio interpreter. Extract `svg_path_parser.{h,cpp}` (stays in `draxul-scoreview`). Enables arc/relative-command/hole-classification tests without real Verovio SVG.
- **`piece_analysis.cpp` (1,066, score):** five independent analyzers (key estimation, chord classification, phrase segmentation, motif detection, rhythm figures) behind one ~590-line `analyze_piece`. Split into per-analyzer TUs in `draxul-score-learn`; each becomes independently testable.

### P2-2 · `analysis_overlay.cpp` (629, score): pure builder trapped in the host TU forces heavy test linkage

**Location:** `modules/score/draxul-scoreview/src/analysis_overlay.cpp` mixes pure `build_analysis_overlay` (`:46`) with NanoVG `draw_*` (`:413/524/563`). Because they share a TU, `scoreview_overlay_tests.cpp` is forced into the full SDL/imgui/nanovg host test target (`tests/CMakeLists.txt:82-90`) just to test geometry. Split into `analysis_overlay_build.cpp` (→ `draxul-scoreview`) and `analysis_overlay_draw.cpp` (stays host); move the test to the lighter `draxul-test-scoreview` target. The header already documents the intended pure/draw split.

### P2-3 · Config field lists duplicated across layers

- **satview:** a setting exists four times — host member, `SatViewConfig`, TOML table (`satview_config_io.cpp`), and panel widget — with `current_config`/`apply_config` (`satview_host.cpp:2251-2403`) mapping ~40 members by hand. Consolidate mapping into `satview_config_io.cpp` as the first step; make `SatViewConfig` the source of truth long-term.
- **`libs/draxul-config/src/config_schema.cpp` (967):** cohesive and table-driven (good), but bundles docs-generation + parse + serialize + type-check over the shared `ConfigFieldDesc` registry. Optional split into `config_docs`/`config_parse`/`config_serialize` sharing the descriptor table. Low priority — the file is data-driven, not tangled.

### P2-4 · Pedagogy/policy leaking upward into `score_host` (module-boundary smell)

`apply_tempo_ladder` (`:899-946`) and `maybe_urgent_rewrite` (`:976-1000`) are learning-domain decisions living in the host; the outcome→`PlayerModel` attribution loop sits in `pump()` (`:1426-1457`). Dependency direction is legal (host→learn) but the *responsibility* is on the wrong side. Move into a `StreamPolicy` / extend `ScoreStreamController` (which already links learn); policy returns caps/verdicts, host applies them to host-owned `flow_`.

### P2-5 · Duplicated stable-contract helpers (drift risk)

- **megacity:** `build_incident_connection_counts`/`building_connection_key` in both `city_builder.cpp` (`:442/:427`) and `city_picking.cpp` (`:195/:224`); `path_has_prefix` in `semantic_city_layout.cpp` (`:39`) and `ui_treesitter_panel.cpp` (`:95`). Consolidate into the existing `src/city_helpers.h`.
- **score:** the "reload full source + clear window geometry" block is duplicated in `relayout()`/`relayout_flow()`; the rewind/restart branch is byte-identical in `apply_inspector_intents` and `on_key`; piece-analysis extraction appears near-verbatim twice (`:462-491` and `:683-710`) and has **already subtly diverged** (one guards an empty profile, the other rebuilds unconditionally). Extract `reload_full_source()`, `rewind_or_restart()`, `analyze_current_piece()`.

### P2-6 · Documentation: missing module `AGENTS.md` for satview and score

Only root and `modules/megacity/` have an `AGENTS.md`. Satview (5-lib stack, simulation worker, `satview_host.cpp` collision risk) and score (6-lib stack, linker-enforced "learn is GPU/transport-free" purity, async-engrave threading, mic-permission TCC ordering) are comparably complex and comparably layered, yet an agent must reverse-engineer their layering from scattered CMake header-comments. Add `modules/satview/AGENTS.md` and `modules/score/AGENTS.md` documenting the library graph/direction, purity rules, threading contracts, and (score) the "SDL must not trigger the consent dialog" rule. Zero build risk, highest agent-isolation payoff per effort.

### P2-7 · `ui_treesitter_panel.cpp` (1,567, megacity): three unrelated panels + a 500-line renderer-controls editor

Split into `ui_object_browser` / `ui_renderer_controls` (the standalone `CodeVizRendererControls` config editor, `:883`) / per-mode previews. Isolates the behavior-bearing config-diff logic from tree-rendering noise; no parity risk (ImGui is cross-platform).

### P2-8 · Renderer runtime asset-path resolution duplicated per backend (satview)

`DRAXUL_REPO_ROOT` injection (`draxul-satview-renderer/CMakeLists.txt:23`) plus per-backend `bundled_asset_path("shaders")` search means dev-vs-deploy shader resolution is defined twice and can diverge in search order. Extract a single `satview_asset_paths` helper both backends call.

---

## Proposed module / target map (deltas only)

```
app/
  app/CMakeLists.txt                    NEW  (own the draxul-app target; root just add_subdirectory)
  WorkspaceManager                      NEW  (owns workspaces_; ChromeHost consumes read API)
  SessionCoordinator                    NEW  (snapshot/persist/checkpoint/restore)

libs/draxul-config/                     opt: split config_schema → docs/parse/serialize

modules/megacity/
  draxul-geometry/       ← receive build_procedural_*_mesh (moved down from city_builder)
  draxul-codeviz-scene/  ← PipelineId manifest (backend-neutral)
  draxul-codeviz-renderer/  State → {ResourceRetirement, PipelineFactory, GBufferTargets,
                                     LabelAtlasResource, TooltipOverlayResource} + gbuffer_debug
  draxul-megacity/       semantic_city_layout → model/layout/grid/routing;
                         RouteBuildService + GridBuildService + CitySelectionController;
                         ui_treesitter_panel → object_browser/renderer_controls/previews
  AGENTS.md              (exists ✓)

modules/satview/
  draxul-satview-scene/  ← receive append_* scene builders; PipelineId manifest
  draxul-satview-core/   ← satview_json / satview_csv extracted from catalog
  draxul-satview-host/   satview_host → ViewController + ControlPanel(model) + picking + object_tree
  AGENTS.md              NEW

modules/score/
  draxul-score-learn/    ← analyze_current_piece helper; piece_analysis → per-analyzer TUs; StreamPolicy
  draxul-scoreview/      ← svg_path_parser; analysis_overlay_build (split from _draw)
  draxul-scoreview-host/ score_host → ScoreLaunchOptions parse + ScorePagedView + status formatter
  AGENTS.md              NEW
```

## Best isolated work packages for parallel agents

These have minimal file overlap and can run concurrently:

1. **Build hygiene** — `app/CMakeLists.txt` extraction (P1-4). Touches only root CMake + new file.
2. **Docs** — `modules/satview/AGENTS.md` + `modules/score/AGENTS.md` (P2-6). Pure docs.
3. **App decomposition** — `WorkspaceManager` + `SessionCoordinator` (P0-1). Confined to `app/`.
4. **Satview host split** — mechanical peer-`.cpp` split then builder-lift (P1-2). Confined to `modules/satview/`.
5. **Score host + learn** — dedup (P2-5), `analysis_overlay` split (P2-2), `piece_analysis` split (P2-1). Confined to `modules/score/`.
6. **Megacity layout split** — `semantic_city_layout` 4-way (P1-3). Confined, pure functions.
7. **Megacity renderer** — retirement-ring + pipeline manifest (P1-1). **Must be single-owner** (paired Vulkan+Metal); do not parallelize across backends.

Packages 3–6 barely touch shared files; package 1 and 7 are the serialization points (both edit CMake / paired backends).

## Strongest existing structural qualities — preserve these

- **Enforced boundaries:** `cmake/CheckDependencyBoundaries.cmake` (configure-time direction checks), `tests/link_isolation/*` (public-header link tests), and per-target `*-test-internals` INTERFACE libs are an excellent, uncommon discipline. Every decomposition above should extend these seams, not bypass them.
- **Sharded per-module test targets** (`draxul-test-core/-app/-megacity/-satview/-scoreview`, `tests/CMakeLists.txt:206-321`) with PCH and label-based CTest — keep test additions in the matching shard.
- **No backend leakage:** Vulkan/Metal types confined to renderer `src/`; PIMPL `State` per backend is the right pattern (the problem is only its internal size and hand-maintained parity, not the split).
- **Exemplar decompositions to copy:** `libs/draxul-host` (`terminal_host_base` + `terminal_host_base_csi` sharing one header), `modules/markdown/draxul-markdown` (layout/parser/host/draw_list/scroll/navigation + per-backend render pass), and score's own `measure_xml` writer seam and `ScoreViewModel`/intent split. These are the template the god-TUs should converge toward.
- **`do.py cmd_hygiene` + `FORBIDDEN_ROOT_NAMES`** already polices stray root artifacts (`NUL.obj`, etc.); the `.gitignore` correctly keeps the six `build-*/` dirs and `*.log` out of the tree.