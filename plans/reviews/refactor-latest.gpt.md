# Repository refactoring review

This was a read-only, static review of `repomix-output.xml`. Four read-only reviewers examined coherent subsystems; I checked their proposed findings against the surrounding source, callers, tests, CMake targets, and available Kanban cards. No files were changed and no build or tests were run. The findings below concern structural boundaries and testability, not runtime defects.

## Findings

### 1. P1 — Make server and configuration tests independently buildable

**Current problem:** `tests/CMakeLists.txt:16-19,136-137` sends every unclassified test to `draxul-test-core`. That target links the renderer, window, server, client, font, and host stacks (`:301-330`), and root `CMakeLists.txt:295-297` makes it depend on the full `draxul` executable. A focused server test such as `tests/server_service_protocol_tests.cpp:1-7` therefore has a much broader build closure than its subject.

**Boundary and migration:** Add focused server and configuration test executables linked to their existing owning libraries; split the broad `tests/support/server_kernel_test_support.h:3-23` only where a narrower fixture needs it. Move independent suites incrementally, preserve the case inventory, then require explicit classification for new suites.

**Testing, agent benefit, risk:** Existing `do.py test --target` support (`do.py:915-924`) can validate each new target without building graphics. Server, config, and renderer agents gain separate build ownership. Keep executable-dependent integration cases in the broad target and preserve Windows/macOS registration.

### 2. P1 — Centralize remote-terminal queue policy in `draxul-client`

**Current problem:** The standalone host worker stages bounded input, request IDs, resize replacement, and scroll coalescing in `libs/draxul-host/src/remote_terminal_host.cpp:244-408`; the coordinator repeats those rules in `libs/draxul-client/src/remote_session_coordinator.cpp:184-309`. Both maintain the same 128-command and 48 KiB limits. The host already privately links `draxul-client` (`libs/draxul-host/CMakeLists.txt:52-56`).

**Boundary and migration:** Put a value-only command queue policy in `draxul-client`, with an injected request-ID source. Migrate one queue at a time; retain each owner's locks, wakeups, worker, and transport.

**Testing, agent benefit, risk:** Test atomic framed input, bounds, coalescing, and IDs directly. This separates transport-policy edits from presentation edits. Preserve the no-coordinator path used by the fake-terminal launch and standalone tests; the normal App path supplies a coordinator (`app/app.cpp:3254-3279`).

### 3. P1 — Separate MegaCity worker ownership from its host

**Current problem:** `plugins/megacity/product/draxul-megacity/include/draxul/megacity_host.h:181-225` gives `MegaCityHost` two thread schedulers, their locks, cancellation, generations, and results. Route publication calls host callbacks from the worker (`src/megacity_host.cpp:592-658`). Tests manipulate host internals and poll elapsed time (`plugins/megacity/tests/megacity_scene_host_tests.cpp:612-670`).

**Boundary and migration:** Extract product-private grid-build and route-build owners with immutable requests and generation-tagged results, one worker at a time. Keep semantic algorithms in their existing model targets and scene publication in the host.

**Testing, agent benefit, risk:** Inject blocked builders for deterministic cancellation/latest-result tests, retaining host integration cases. Worker and UI work can then proceed independently. Preserve callback lifetime, nonblocking retirement, and shutdown order; the two schedulers need not be made identical.

### 4. P1 — Share NanoVG CPU batch construction across Metal and Vulkan

**Current problem:** The Metal fill/stroke builders (`libs/draxul-nanovg/backend/src/nanovg_mtl.mm:636-752`) and Vulkan equivalents (`backend/src/nanovg_vk.cpp:1158-1267`) repeat fan conversion, path offsets, cover geometry, and uniform-block decisions. Paint conversion already has a shared private seam. Both implementations are selected in the same backend target (`libs/draxul-nanovg/CMakeLists.txt:33-53`).

**Boundary and migration:** Add a backend-private C++ builder for neutral draw-batch records. Move triangles first, then stroke and fill; leave native blending, resources, and command encoding in each backend.

**Testing, agent benefit, risk:** Extend device-free batch tests and retain both render snapshots. Geometry changes then have one owner and a narrow validation path. Preserve uniform layout and draw order on both GPUs.

### 5. P2 — Split SatView CPU tests from GPU/runtime tests

**Current problem:** `plugins/satview/cmake/Tests.cmake:2-30` globs all C++ suites into one target linked to runtime, renderer internals, SDL, and the core renderer; on macOS it also depends on Metal shader compilation. Catalog and scene-composer tests need only the already separate core and scene libraries (`plugins/satview/CMakeLists.txt:31-66`).

**Boundary and migration:** Classify CPU/service/scene suites separately from runtime/GPU suites. Move catalog and composer cases first, retain the `satview` aggregate and labels, and check the case inventory.

**Testing, agent benefit, risk:** Catalog and scene agents can build their actual dependency closure. Assign shader-text, asset, and renderer-internal cases deliberately; sharding the current executable would leave its broad links intact.

### 6. P2 — Isolate Rezonality’s compiler adapter

**Current problem:** `plugins/rezonality/src/live_project.cpp` combines platform process execution (`:203`), scene parsing (`:613`), compiler diagnostic decoding (`:853`), shader compilation (`:920`), candidate construction (`:1281`), and watching (`:1502`). Its test injection supplies already-decoded diagnostics (`src/live_project.h:139-155`; `tests/rezonality_project_tests.cpp:414-444`), leaving production decoding tied to a real compiler.

**Boundary and migration:** Keep the existing `draxul-rezonality-project` target (`plugins/rezonality/CMakeLists.txt:77-99`), but move process running and diagnostic translation into private compiler-adapter files with an injectable typed process result. Keep `ProjectPipeline` and `LiveProject` APIs stable.

**Testing, agent benefit, risk:** Recorded process results can cover spawn failure, exit status, and diagnostic formats; retain real edit/reload tests. Compiler, scene grammar, and watcher changes gain separate files. Preserve platform argument quoting, path handling, diagnostic limits, and temporary-output ownership.

### 7. P2 — Put SatView surface projection in its scene layer

**Current problem:** Surface visibility, positions, representative selection, and map-wrap markers live in runtime helpers (`plugins/satview/src/runtime/satview_runtime.cpp:2017-2274`). Drawing uses them at `:1317-1353`, while picking repeats visibility and position decisions at `:3203-3244`. Satellite markers already use a typed scene-composer request (`:1290-1305`); `draxul-satview-scene` is the existing target (`plugins/satview/CMakeLists.txt:57-66`).

**Boundary and migration:** Add a value-only surface composition request and visible-surface projection to that scene target. First move the helpers; then have drawing and picking consume the same projection. Runtime keeps camera state, renderer calls, and screen-space hit testing.

**Testing, agent benefit, risk:** Test filtering, selected-object overrides, lunar/Mars placement, and map seams without a GPU, then retain render scenarios. This separates surface composition from runtime work. Preserve selected-item visibility and pick behavior for wrapped markers.

### 8. P2 — Extract Kanban interaction timing from `KanbanHost`

**Current problem:** `modules/kanban/draxul-kanban/src/kanban_host.cpp:231-306` combines file-monitor debounce, held-key timing, focus/preview effects, and drawing in its pump. Host tests initialize rendering and fonts and use timed waits (`tests/kanban_host_tests.cpp:100-164,574-650`). A completed regression card documents interference between watcher debounce and a repeat/focus test.

**Boundary and migration:** Add a Kanban-owned state object that accepts timestamps, key/focus events, and watcher invalidations and returns due effects. Keep native monitoring, filesystem reload, callbacks, and drawing in the host. Move repeat/focus first, then debounce/preview; retain existing navigation command mapping. The current core/host target split supports this (`modules/kanban/draxul-kanban/CMakeLists.txt:1-32`).

**Testing, agent benefit, risk:** Deterministic event sequences replace sleeps for policy tests, while native-monitor tests remain. Navigation and monitoring edits gain separate ownership. Preserve deferred preview refresh after focus returns.

### 9. P2 — Give server agent commands a private policy seam

**Current problem:** `libs/draxul-server/src/server_kernel_requests.cpp:529-623` begins a large agent route that handles request-ID validation and replay alongside launch, restart, input, and report policy. `ServerAgentService` handles projection/read behavior, while replay state sits in `ServerSession`. Policy tests consequently use the broad kernel and real shell setup.

**Boundary and migration:** Add a private agent-command collaborator within `draxul-server`, accepting typed operations for effects and snapshot lookup. Move validation, successful-result replay, and response mapping incrementally; leave authentication, Session resolution, state-thread scheduling, and terminal ownership in the kernel. The target already exposes a test-only private seam (`libs/draxul-server/CMakeLists.txt:30-36`).

**Testing, agent benefit, risk:** Fake-operation tests can cover policy without launching a shell, while process integration remains. Agent-command work stops colliding with central request routing. Preserve authentication order, error precedence, replay limits, and generation checks.

### 10. P2 — Consolidate PID-based process identity lookup

**Current problem:** Client discovery (`libs/draxul-client/src/server_client.cpp:126-208`) and server startup (`libs/draxul-server/src/server_kernel_lifecycle.cpp:44-106`) separately derive the same start token on macOS and Linux. The shared `process_util` currently covers the Windows handle form only; tests start a kernel merely to obtain a token (`tests/support/server_kernel_test_support.h:55-83`). Both targets already link `draxul-types`.

**Boundary and migration:** Add the PID operation to the existing `draxul-types` process utility and migrate producer and consumer together. Keep the client's explicit liveness check and held-handle force-stop protection.

**Testing, agent benefit, risk:** Direct identity tests replace the test-only kernel startup; retain lifecycle/discovery tests. One owner maintains token formatting across platforms. Token bytes and PID-reuse protection must remain stable.

### 11. P2 — Share primitive ImGui event injection

**Current problem:** Diagnostics input (`libs/draxul-ui/src/ui_panel.cpp:332-408`) and plugin input (`plugins/support/imgui/include/draxul/imgui_input_bridge.h:25-100`) repeat modifier, text, button, and wheel injection. `draxul-imgui-core` already owns their shared scancode table and is intentionally limited to ImGui and SDL headers (`libs/draxul-imgui-core/CMakeLists.txt:1-18`).

**Boundary and migration:** Add scalar event-injection functions to that existing core target, then delegate from both wrappers. Keep diagnostics coordinate scaling and plugin capture verdicts in their respective wrappers.

**Testing, agent benefit, risk:** Context-only tests can cover button mapping, modifiers, text, and wheel; wrapper tests retain scaling/capture checks. Core and product input agents update one mapping. Keep the core free of `draxul-types` and preserve the plugin bridge’s current-context behavior.

### 12. P2 — Use the glyph-atlas seam for palette and toast layout

**Current problem:** Palette and toast render functions require `TextService` (`libs/draxul-gui/src/palette_renderer.cpp:72-86`; `toast_renderer.cpp:79-86`), although their layout paths only resolve clusters (`palette_renderer.cpp:114`; `toast_renderer.cpp:148`). Layout tests initialize a real font and skip when unavailable (`tests/overlay_unicode_tests.cpp:74-102`). The existing `IGlyphAtlas` exposes cluster resolution (`libs/draxul-types/include/draxul/glyph_atlas.h:17-34`).

**Boundary and migration:** Accept `IGlyphAtlas&` in palette, toast, and their occlusion helper; pass explicit regular-style flags. Existing App callers can pass `TextService`. Keep tooltip bitmap work on concrete font services.

**Testing, agent benefit, risk:** Use the existing fake atlas for deterministic clipping, continuation-cell, and coloring tests, retaining real-font integration tests. Overlay agents gain an isolated layout seam. This change alone does **not** remove the GUI target’s font link (`libs/draxul-gui/CMakeLists.txt:20-24`); preserve atlas reset ordering.

### 13. P2 — Separate Markdown parser tests from host cases

**Current problem:** `tests/markdown_parser_tests.cpp:224-333` includes live `MarkdownHost`, font/DPI, and preview cases after parser cases. `tests/CMakeLists.txt:87-109,488-496` puts nearly all Markdown suites in a target linked to `draxul-markdown-host`, which brings renderer and native backend links (`modules/markdown/draxul-markdown/CMakeLists.txt:34-68`).

**Boundary and migration:** Move host cases to a host suite and classify parser/front-matter/scroll cases under a core-linked test target; preserve case registration and keep real-font cases where needed. Leave SDL-dependent navigation in the host target initially.

**Testing, agent benefit, risk:** Parser agents can validate without a GPU/backend build, while host integration remains available. Check both platform target closures after reclassification.

### 14. P2 — Correct central agent ownership guidance

**Current problem:** `CLAUDE.md:28-29` directs product-specific behavior under `modules/`, although its own inventory assigns five initialized product repositories under `plugins/` (`:95-108`). Its test-scope list at `:259-262` also omits the implemented `--rezonality` option (`do.py:942-953`).

**Boundary and migration:** Amend the central guide to distinguish built-in `modules/` from plugin-owned `plugins/<product>/` repositories and list all five product test scopes. Keep the existing product README and local guidance as the detailed owners.

**Testing, agent benefit, risk:** A documentation consistency check against root CMake and `do.py` is sufficient. Agents get the correct repository and narrow validation command; no runtime or platform behavior changes.

## Scope notes and unresolved lead

The packed directory and file sections contain no root `pending/` or `ice-box/` card bodies, although `kanban/.draxul-kanban.toml:4-5` still names many historical cards. I excluded implemented boundaries and the product cards actually present, including SatView’s deferred atmosphere consolidation. The absent card bodies limit any claim about their historical intent.


## Proposed target map and parallel work

| Owner | Proposed boundary |
|---|---|
| `draxul-client` | Private remote command queue policy; host and coordinator retain their drivers |
| `draxul-types` | PID identity operation in existing process utility |
| `draxul-nanovg-backend` | Private, backend-neutral CPU batch builder |
| `draxul-server` | Private agent-command policy collaborator |
| `draxul-kanban`, `draxul-satview-scene` | Value-only interaction timing and surface projection respectively |
| MegaCity, Rezonality product targets | Private worker owners and compiler adapter |
| Test CMake | Narrow server/config, Markdown core/host, and SatView CPU/runtime executables |

The best independent agent packages are core test classification, client queue policy, NanoVG batch construction, MegaCity worker ownership, and SatView test classification. Preserve the existing protocol/transport, host-leaf, plugin ABI/allowlist, and Vulkan/Metal target boundaries; they give these packages a clear dependency direction.

## Coverage

| Area | Representative paths or flows examined | Reviewer | Remaining gaps |
|---|---|---|---|
| App, config, orchestration | `app/app.cpp` initialization/pump/control, config schema and CMake, `do.py` | Coordinator | Not every App callback or config field |
| Client, server, control, Session | Coordinator/host delivery, kernel requests/lifecycle, protocol and test seams | Worker; coordinator verified findings | Not every request method or transport branch |
| Terminal, Neovim, PTY, built-ins | Host leaves, terminal process/core interfaces, Neovim split, Markdown/Kanban flows | Worker; coordinator verified findings | Not every VT handler or native process branch |
| Rendering, fonts, platforms, SDK support | NanoVG Metal/Vulkan, GUI/ImGui, font/atlas interfaces, render targets | Worker; coordinator verified findings | Native encoder and shader bodies not exhaustively read |
| MegaCity | Host workers, model/renderer targets, tests and guidance | Worker; coordinator verified finding | Full GPU backend internals |
| SatView | Core/scene/services/runtime, picking, tests and guidance | Worker; coordinator verified findings | Full render backend internals |
| ScoreView | Target graph, runtime seams, tests, README and Kanban | Worker | Detailed notation/audio algorithms |
| Rezonality | Project pipeline, compiler/watch flow, targets, tests and guidance | Worker; coordinator verified finding | Full Vulkan/Metal renderer internals |
| Build, tests, tracker | Root/product CMake, focused targets, `do.py`, available Kanban lanes | Coordinator and workers | No builds or tests run; absent root card bodies cannot be inspected |
