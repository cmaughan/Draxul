# Refactoring consensus

Static inspection only; indexed review hashes matched. No files were changed and no builds or binaries were run. There are no P0 refactors.

## Accepted refactors

### 1. Split control-plane transport privately

- Priority: P1; high leverage, elevated cross-platform risk.
- Proposed by: Anthropic Claude Opus and OpenAI Codex.
- Evidence: `libs/draxul-control/src/control_plane.cpp` is 1,944 lines with interleaved Win32/POSIX helpers, separate `ControlServer::Impl::run` implementations at lines 1370 and 1532, and branched `ControlClient::request` logic at 1732–1892. Its CMake target compiles one source. `tests/control_plane_tests.cpp` is 1,371 lines and is App-classified because it also tests `control_cli`.
- Boundary: retain one `draxul-control` static library and the unchanged `control_plane.h` API. Add private common codec/deadline and metadata/cache units plus platform-selected Win32 and POSIX transport units.
- Callers/dependencies: `draxul-client`, `draxul-server`, and App remain unchanged; JSON stays public. Win32 ACL/pipe handles, POSIX locks/socket descriptors, polling, and endpoint ownership stay private.
- Migration: extract pure framing first, then metadata/cache, platform primitives, client exchange, and finally server listeners. Preserve the two run-loop behaviors mechanically before considering common-loop consolidation.
- Tests: split codec/transport tests into core-owned files; retain CLI/App integration separately. Add `draxul-control-test-internals`.
- Parity: preserve absolute request deadlines, same-user security, stale-endpoint serialization, endpoint abandonment, nonblocking shutdown, POSIX `O_NONBLOCK` clearing, and `EINTR` retries.
- Dependencies: close or freeze pending cards `09` and `10` first. One control owner establishes the seam; platform moves become independent only afterward.

### 2. Decompose `ServerKernel::Impl` inside the existing library

- Priority: P1; highest source-contention reduction, sensitive state risk.
- Proposed by: Claude, Codex, and xAI Grok.
- Evidence: `server_kernel.cpp` is 3,592 lines; `Impl` begins at line 339, `handle_request` spans 1899–2874, and `run_until_stopped` spans 3216–3528. It owns clients, restore/checkpoints, topology, agents, terminals, publication, status, and event-loop state. `server_kernel_tests.cpp` is 4,928 lines. `server_agent_service.h` is public but is used only by the kernel and `agent_protocol_tests.cpp`.
- Boundary: keep one `draxul-server` static library and the public `ServerKernel` PImpl API. Introduce a private `server_kernel_impl.h` and mechanical lifecycle, client, session/checkpoint, request, terminal/agent, and event-loop TUs. Do not create service static libraries initially.
- Callers/dependencies: public callers and links remain unchanged. Move `ServerAgentService` to `src/` and expose it only through `draxul-server-test-internals`.
- Migration: split declarations/definitions first; split the test file by method family; only then introduce proven collaborators such as `ServerClientRegistry` or `ServerSessionStore`. Do not add a second mutex or speculative `KernelFacade`.
- Tests: focused lifecycle/discovery, clients, persistence, topology/terminal, agent-control, and codec/resource suites, still aggregated under the core label.
- Parity: keep platform publication/process code in lifecycle units; preserve lock ownership, task lifetime, generation checks, leases, idempotency caches, and shutdown order.
- Dependencies: follows the control split and completion/freeze of pending `09`, `10`, and `14`. One server owner freezes private state; test-file splitting can then proceed independently.

### 3. Create a pure App-shell static library

- Priority: P1; low behavioral risk and strong test-isolation benefit.
- Proposed by: Claude and Grok.
- Evidence: `split_tree`, `chrome_layout`, `app_shell_layout`, `chrome_pill`, `fuzzy_match`, and `rename_editor` total about 2,600 lines of deterministic layout/editing logic but compile into `draxul-app`, whose public link closure contains 17 libraries. Their four principal tests are App-classified. `PaneDescriptor` is a two-field value incorrectly owned by the renderer; `SystemResourceSnapshot` is similarly bundled with its runtime monitor. `draxul-app-support` is an empty INTERFACE bundle used only by `draxul-test-core`.
- Boundary: add `draxul-app-shell` containing those pure sources and public headers. Move `PaneDescriptor` and `SystemResourceSnapshot` to `draxul-types`; update includes directly, with no duplicate forwarding headers. Retire `draxul-app-support`.
- Callers/dependencies: `draxul-app` and a new `draxul-test-app-shell` call the library. Public dependencies are `draxul-types`, `draxul-session-model`, and required config value types; performance remains private. No renderer, window, host, Nvim, NanoVG, ImGui, or runtime-support dependency.
- Migration: move value records, establish the target with fuzzy/shell layout, then split tree, then chrome/pill/rename. Reclassify tests after each source move.
- Tests: a focused `app-shell` target/label for split geometry, layout, pill, rename, and fuzzy matching.
- Parity: CPU-only outputs must remain identical on MSVC and Apple Clang; no Vulkan/Metal behavior changes.
- Dependencies: pending `00` and `01`. One app/build owner controls target and test classification.

### 4. Give shaders explicit module ownership and option gates

- Priority: P1; major collision and incremental-build reduction, high backend parity risk.
- Proposed by: Claude and Grok.
- Evidence: root `shaders/` contains 64 Vulkan entry points: 36 SatView, 16 Megacity, four Markdown, and eight core GUI/grid/NanoVG shaders. `CompileShaders.cmake` globs all entry points and makes every root include a dependency of every shader. Windows always builds and stages all product shaders. Metal has seven repetitive compile/link groups and gates only executable dependencies.
- Boundary: retain core/shared shaders under root ownership; move Markdown, Megacity, and SatView shaders beside their owning renderer modules. Create explicit shader-group custom targets with stable output names and one shared CMake API.
- Callers/dependencies: renderer-owning module CMake files declare their shader group. Optional groups exist only when their product option is enabled. Runtime lookup and final staged filenames remain unchanged.
- Migration: first complete/reuse `kanban/ice-box/06 metal-shader-dependency-closure -bug.md`; record the current output manifest; express existing groups explicitly; gate Vulkan product groups; then move one module at a time, SatView first.
- Tests: compare old/new output manifests, configure all-optionals-off and one-at-a-time, build each shader group independently, and run relevant render snapshots.
- Parity: never rename `.spv` or `.metallib` outputs during the move; preserve shared include closure, bindings, formats, and staging on both backends.
- Dependencies: pending `00`, `01`, `05`, `06`, and ice-box `06`. One shader/build owner freezes the helper and output manifest; module-local moves can then be independent.

### 5. Move Unicode implementation into `draxul-types`

- Priority: P2; low architectural risk, broad compile-time benefit.
- Proposed by: Codex.
- Evidence: `unicode.h` is 672 lines and contains large range tables, decoding, width, validation, clustering, and vector-producing implementations inline. It is directly included by 22 source/test files across App, font, grid, GUI, Nvim, terminal, Markdown, Kanban, and SatView. `draxul-types` currently compiles only logging and runtime-path sources.
- Boundary: preserve all names, signatures, value records, and ownership in `draxul-types`; add private `src/unicode.cpp`. Leave only genuinely trivial or performance-justified helpers inline.
- Callers/dependencies: unchanged.
- Migration: move tables/classification first, then clustering/vector helpers; benchmark hot scalar decoding/width helpers before removing inline definitions.
- Tests: existing Unicode, grid, UI-event, font, terminal, and render suites; add Unicode use to the types link-isolation consumer.
- Parity: pin malformed UTF-8, replacement behavior, ambiguous width, emoji presentation, ZWJ, modifiers, regional pairs, and Indic virama behavior on MSVC and Apple Clang.
- Ownership: one foundation/types owner; independent of product work.

### 6. Separate ScoreView keyboard layout from NanoVG replay

- Priority: P2; small, clear boundary with immediate test isolation.
- Proposed by: Codex.
- Evidence: public `keyboard_render_nvg.h` combines MIDI geometry, palette functions, `KeyboardLit`, `NVGcontext`, and drawing. `keyboard_layout.cpp` is in `draxul-scoreview`; `draw_piano_keyboard` is implemented only in `draxul-scoreview-host`. Geometry/palette tests occupy lines 541–690 of the host-classified 1,221-line `scoreview_composer_tests.cpp`.
- Boundary: public core `keyboard_layout.h` owns MIDI bounds, geometry, palette values, and palette selection. A host-private header owns `KeyboardLit` and NanoVG replay.
- Callers/dependencies: `draxul-scoreview` remains renderer-free; `draxul-scoreview-host` consumes layout plus NanoVG. No new static library.
- Migration: introduce the core header, update includes, move host declarations, extract `scoreview_keyboard_layout_tests.cpp`, then remove the mixed header without forwarding duplication.
- Tests: keyboard geometry/palette tests link only `draxul-scoreview`; retain one host draw smoke if useful.
- Parity: NanoVG visuals and palette bytes remain identical on Windows/Vulkan and macOS/Metal.
- Dependencies: land immediately before or with pending `07`, which touches adjacent overlay/palette ownership. One ScoreView owner.

### 7. Enforce product dependency direction in CMake

- Priority: P2; low runtime risk and useful architectural leverage.
- Proposed by: Claude.
- Evidence: `CheckDependencyBoundaries.cmake` protects ten foundation targets but does not reject `draxul-app` links to optional products, cross-product links, or product links from renderer/UI/config/font/window infrastructure. These rules currently exist only in comments and guidance.
- Boundary: extend the existing configure-time checker; do not create another checking system. Mark internal targets with a product family after pending `00`, reject core-to-product and cross-product direct/interface links, and exempt only the executable integration root.
- Scope: link/interface edges only. Shader/source/asset isolation remains the shader card’s responsibility.
- Tests: positive current-tree checks, negative CMake fixtures, all-optionals-off, and one-product-enabled configurations.
- Parity: configure-only and platform-neutral; exercise both Windows and macOS generators.
- Dependencies: pending `00`. One CMake-policy owner.

## Rejected or reconciled proposals

| Proposal | Decision |
|---|---|
| Minimal optional-module CI command | No new card. Pending `00` already requires optional-off configuration, and ice-box `23 core-extra-repo-split -architecture.md` owns the eventual public-only CI matrix. |
| Further `draxul-test-core` partition | Defer. `done/35 modular-test-targets` deliberately established seven stable executables; pending `02`, `03`, and `08` will change target/label ownership again. The App-shell target supplies the immediate missing isolation. |
| Broad `draxul-runtime-support` split | Defer until pending host/Nvim splits settle. `UiRequestWorker` is already tracked in ice-box `23`; pane-print reliability has its own card. Moving `SystemResourceSnapshot` is accepted only as an App-shell prerequisite. |
| Metal renderer multi-TU split | Already tracked by ice-box `25 renderer-backend-parity-cleanup -refactor.md`. |
| Score audio PUBLIC→PRIVATE | Drop. `done/22 scoreview-module-boundaries` deliberately retained transitive test linkage; changing it requires a fresh consumer audit rather than reversing that completed decision speculatively. |
| Control CLI/weather libraries | Drop for now. App itself still consumes control and HTTP services, so the proposed targets do not materially narrow its production closure. |
| `app_entry` callable dispatcher | Drop as speculative and overlapping active `main.cpp` work in pending `10`, ice-box `23`, and `54`. |
| Dead cube-pass deletion | Not accepted without repository history/owner confirmation; this snapshot has no Git metadata. The stale architecture guidance is already tracked by pending `08`. |
| App test globs/target discovery | Fold App-shell tests into their focused target and leave general label/coverage discovery to pending `08`. |
| Delete `draxul-app-support` | Folded into the App-shell card. |
| Per-library documentation sweep | No standalone sweep. Add dependency-contract comments when each accepted target changes; canonical and nested guidance remains pending `08`. |
| Server service static libraries | Rejected initially as excessive granularity. Establish private TUs/collaborators inside `draxul-server` first. |

## Target map

Caller points toward dependency:

```text
draxul
├── draxul-app
│   └── draxul-app-shell [new STATIC]
│       ├── draxul-session-model
│       ├── draxul-config value APIs
│       └── draxul-types
├── draxul-server [same public STATIC]
│   ├── private lifecycle/client/session/request/event-loop TUs
│   ├── private ServerAgentService
│   └── draxul-control / protocol / session-model / terminal-process / types
└── product hosts

draxul-client / draxul-server
└── draxul-control [same public STATIC]
    ├── private codec + metadata/cache
    ├── private Win32 pipe/ACL transport
    └── private POSIX socket/lock transport

draxul-types
├── PaneDescriptor
├── SystemResourceSnapshot
└── unicode declarations + private unicode.cpp

draxul-scoreview
└── keyboard layout/palette
    ↑
draxul-scoreview-host
└── private KeyboardLit + NanoVG replay

shader custom targets
├── core/shared
├── markdown
├── megacity [optional]
└── satview [optional]
```

## Repository guidance

Pending `08 agent-guidance-label-validation -refactor.md` remains the sole guidance/tooling owner and should additionally:

- Correct the stale `I3DRenderer`/`I3DHost`/registration descriptions in `CLAUDE.md`, `docs/module-map.md`, and active architecture documentation.
- Replace obsolete Megacity test binary/file instructions and add SatView/Score nested guides.
- Keep root `AGENTS.md` a thin pointer.
- Add the proposed `app-shell` label and new target mappings to `do.py test --label`.
- Stop hard-coding coverage target lists where CMake can publish the authoritative set.
- Require a short dependency-contract comment beside every new or materially changed target.
- Update the module map after each boundary lands; do not rewrite historical plans or retrospective learnings as though they were current contracts.

## Sequence

1. Land pending `00`, then pending `01`; close or freeze active control/server cards `09`, `10`, and `14`.
2. In parallel where file ownership permits: Unicode extraction and product-boundary enforcement.
3. Control transport split.
4. Server private decomposition.
5. App-shell library after root App CMake ownership is local.
6. Score keyboard boundary immediately before/with pending `07`.
7. Complete ice-box shader dependency closure, then shader grouping/gating and module-local moves.
8. Land pending `08` after target names and labels stabilize.

## Proposed work items

### `kanban/pending/12 control-transport-boundary -refactor.md`

# Split control common logic from platform transport

**Type:** refactor  
**Priority:** P1  
**Raised by:** Claude and Codex  
**Depends on:** pending `09` and `10` completion/freeze

## Boundary verification

- [ ] Inventory every Win32/POSIX helper, deadline, metadata/cache, framing, listener, and client responsibility.
- [ ] Record behavioral differences between the two server run loops before moving code.
- [ ] Pin public `control_plane.h` API and security/shutdown invariants.
- [ ] Split CLI/App cases from transport cases in the test inventory.

## Implementation and migration

- [ ] Extract common codec/deadline helpers without behavior change.
- [ ] Extract metadata/cache helpers.
- [ ] Add private platform-selected Win32 and POSIX transport sources.
- [ ] Move client exchange branches, then listener loops.
- [ ] Consolidate common loops only where recorded behavior is identical.
- [ ] Add `draxul-control-test-internals`.

## Unit tests

- [ ] Test frame limits, malformed JSON, depth, absolute deadlines, and partial I/O without live sockets.
- [ ] Test stale metadata, concurrent ownership, stop-pending, and listener recovery.
- [ ] Build `draxul-control` and `draxul-test-core`; run CTest label `core`.

## Cross-platform validation

- [ ] Windows: ACLs, named-pipe ownership, overlapped cancellation, and abandonment.
- [ ] macOS: locks, socket ownership, accepted-fd blocking mode, `EINTR`, and shutdown.
- [ ] Compare timeout/error/reconnect behavior across platforms.

## Agent documentation/tooling

- [ ] Add a local dependency/transport contract comment.
- [ ] Update `docs/module-map.md` without exposing backend types.

## Acceptance criteria

- [ ] Public API and callers are unchanged.
- [ ] Platform code is absent from common transport sources.
- [ ] Focused transport tests no longer require `draxul-app`.
- [ ] Both platform suites and smoke remain green.

### `kanban/pending/13 server-kernel-private-decomposition -refactor.md`

# Decompose `ServerKernel::Impl` behind its façade

**Type:** refactor  
**Priority:** P1  
**Raised by:** Claude, Codex, and Grok  
**Depends on:** card `12`; pending `09`, `10`, and `14` completion/freeze

## Boundary verification

- [ ] Inventory all `Impl` state and methods by lifecycle, clients, sessions, requests, terminals/agents, and event loop.
- [ ] Record lock, task, generation, lease, cache, and shutdown invariants.
- [ ] Inventory public `ServerAgentService` consumers.
- [ ] Partition the 4,928-line test suite without losing tags/cases.

## Implementation and migration

- [ ] Add private `server_kernel_impl.h`.
- [ ] Move member definitions mechanically into responsibility TUs.
- [ ] Move `ServerAgentService` to `src/`.
- [ ] Add `draxul-server-test-internals`.
- [ ] Split tests by responsibility.
- [ ] Introduce registry/store collaborators only after state ownership is proven.
- [ ] Do not add service static libraries or another mutex.

## Unit tests

- [ ] Add focused lifecycle/discovery, client/authentication, checkpoint, topology/terminal, agent, and resource suites.
- [ ] Build `draxul-server` and `draxul-test-core`; run CTest label `core`.
- [ ] Preserve the server public-header link-isolation build.

## Cross-platform validation

- [ ] Validate publication, process identity, eviction, signals, and terminal lifecycle on Windows and macOS.
- [ ] Confirm the server remains renderer/window/host/product free.

## Agent documentation/tooling

- [ ] Document private state/lock ownership beside the implementation.
- [ ] Update the server row in `docs/module-map.md`.

## Acceptance criteria

- [ ] `ServerKernel` public API is unchanged.
- [ ] No implementation TU owns unrelated method families.
- [ ] `ServerAgentService` is no longer production-public.
- [ ] Focused/full tests and headless smoke remain green.

### `kanban/pending/15 app-shell-static-library -refactor.md`

# Extract the pure App-shell library

**Type:** refactor  
**Priority:** P1  
**Raised by:** Claude and Grok  
**Depends on:** pending `00` and `01`

## Boundary verification

- [ ] Inventory callers and dependencies of split tree, shell/chrome layout, pill, rename, and fuzzy matching.
- [ ] Verify `PaneDescriptor` and `SystemResourceSnapshot` are neutral value records.
- [ ] Capture current App test classification and public include paths.
- [ ] Confirm `draxul-app-support` has no consumer beyond core tests.

## Implementation and migration

- [ ] Move the two neutral records to `draxul-types`, with direct include migration and no forwarding duplicates.
- [ ] Add `draxul-app-shell` and public headers.
- [ ] Move fuzzy/shell layout, split tree, then chrome/pill/rename sources.
- [ ] Link `draxul-app` to the new target.
- [ ] Add `draxul-test-app-shell` and label.
- [ ] Retire `draxul-app-support` and replace its sole consumer with explicit links.

## Unit tests

- [ ] Move split, shell-layout, chrome-layout, fuzzy, and rename state tests.
- [ ] Add a public-header/link-isolation consumer.
- [ ] Build only `draxul-app-shell` and `draxul-test-app-shell`; run label `app-shell`.

## Cross-platform validation

- [ ] Compare deterministic results on MSVC and Apple Clang.
- [ ] Verify no renderer/window/host/Nvim/NanoVG/ImGui link enters the target.
- [ ] Run App tests and render smoke on both backends.

## Agent documentation/tooling

- [ ] Add dependency-contract comments.
- [ ] Update module map and pending `08` label mapping.

## Acceptance criteria

- [ ] Pure shell tests build without the App/product/GPU closure.
- [ ] `draxul-app-support` is removed.
- [ ] App behavior, geometry, and visuals are unchanged.
- [ ] Full build, tests, render snapshots, and smoke remain green.

### `kanban/pending/16 product-shader-ownership -refactor.md`

# Localize and gate product shaders

**Type:** refactor  
**Priority:** P1  
**Raised by:** Claude and Grok  
**Depends on:** pending `00`, `01`, `05`, `06`; ice-box `06`

## Boundary verification

- [ ] Record every shader entry point, include, owner, output filename, runtime lookup, and staging rule.
- [ ] Capture the current Vulkan and Metal output manifests.
- [ ] Identify genuinely shared headers.
- [ ] Verify product OFF configurations currently compile no product sources except the known shader leak.

## Implementation and migration

- [ ] Complete/reuse the explicit Metal dependency helper from ice-box `06`.
- [ ] Extend the helper to explicit cross-platform shader groups.
- [ ] Replace root Vulkan globs with explicit core/product groups.
- [ ] Gate Megacity and SatView groups.
- [ ] Move Markdown, SatView, and Megacity shaders to owning modules one at a time.
- [ ] Preserve runtime filenames and staging destinations.

## Unit/build tests

- [ ] Compare emitted output manifests before and after grouping.
- [ ] Build each shader group independently.
- [ ] Run shader ABI tests and relevant render snapshots.
- [ ] Configure all-optionals-off and one-product-enabled matrices.

## Cross-platform validation

- [ ] Compile/stage Vulkan SPIR-V on Windows.
- [ ] Compile/stage Metal libraries on macOS.
- [ ] Verify shared include changes rebuild only dependent groups.
- [ ] Confirm visual/pass-order parity.

## Agent documentation/tooling

- [ ] Update module-local shader ownership guidance.
- [ ] Add dependency-contract comments to shader-owning CMake files.

## Acceptance criteria

- [ ] Product OFF builds emit no product shaders.
- [ ] Routine product shader work does not edit root shader wiring.
- [ ] Output names and runtime lookup remain stable.
- [ ] Both backend builds and render suites pass.

### `kanban/pending/17 unicode-implementation-tu -refactor.md`

# Move Unicode implementation out of the public header

**Type:** refactor  
**Priority:** P2  
**Raised by:** Codex

## Boundary verification

- [ ] Inventory all 22 direct consumers and required API declarations.
- [ ] Classify tables, high-level helpers, and hot scalar helpers.
- [ ] Record existing malformed-input, width, emoji, and cluster behavior.

## Implementation and migration

- [ ] Add `libs/draxul-types/src/unicode.cpp`.
- [ ] Move range tables and classification logic.
- [ ] Move clustering and vector-producing helpers.
- [ ] Retain only measured/trivial inline helpers.
- [ ] Preserve all names, signatures, defaults, and replacement behavior.

## Unit tests

- [ ] Run Unicode, grid, UI-event, font, terminal, and overlay suites.
- [ ] Add Unicode use to types link isolation.
- [ ] Benchmark hot cell-width/decoding paths before and after.

## Cross-platform validation

- [ ] Validate MSVC and Apple Clang behavior.
- [ ] Pin malformed UTF-8, ambiguous width, emoji/ZWJ/modifier, regional-pair, and Indic cases.

## Agent documentation/tooling

- [ ] Document that Unicode remains owned by `draxul-types`.
- [ ] Update the target source inventory.

## Acceptance criteria

- [ ] The public header contains declarations and only justified inline code.
- [ ] Callers and behavior are unchanged.
- [ ] No measurable rendering regression is introduced.
- [ ] Focused/full tests pass.

### `kanban/pending/18 scoreview-keyboard-layout-boundary -refactor.md`

# Separate ScoreView keyboard layout from NanoVG replay

**Type:** refactor  
**Priority:** P2  
**Raised by:** Codex  
**Depends on:** coordinate with pending `07`

## Boundary verification

- [ ] Inventory geometry, palette, presentation values, draw callers, and tests.
- [ ] Confirm which overlay code requires palette APIs.
- [ ] Record palette bytes and representative key positions.

## Implementation and migration

- [ ] Add public core `keyboard_layout.h`.
- [ ] Move MIDI bounds, geometry, and palette APIs into it.
- [ ] Move `KeyboardLit` and draw declarations to a host-private header.
- [ ] Update core/host includes.
- [ ] Remove `keyboard_render_nvg.h` without forwarding duplication.
- [ ] Extract keyboard tests from the host-classified composer suite.

## Unit tests

- [ ] Add `scoreview_keyboard_layout_tests.cpp`.
- [ ] Test 88-key bounds, black/white classification, centers, spelling, and palette selection.
- [ ] Keep one host-level draw smoke if useful.
- [ ] Build/run `draxul-test-scoreview` and `draxul-test-scoreview-host`.

## Cross-platform validation

- [ ] Verify NanoVG output and palette bytes on Windows and macOS.
- [ ] Confirm core keyboard tests link no host/NanoVG/SDL/ImGui dependency.

## Agent documentation/tooling

- [ ] Update the Score nested guide/module map under pending `08`.

## Acceptance criteria

- [ ] Core publishes no `NVGcontext` keyboard API.
- [ ] Geometry/palette tests use only `draxul-scoreview`.
- [ ] Visual output is unchanged.
- [ ] Score core/host tests pass.

### `kanban/pending/19 product-dependency-boundary-enforcement -refactor.md`

# Enforce product dependency direction

**Type:** refactor  
**Priority:** P2  
**Raised by:** Claude  
**Depends on:** pending `00`

## Boundary verification

- [ ] Inventory every internal target and assign core/Markdown/Kanban/Megacity/SatView/Score family ownership.
- [ ] Record allowed integration edges and the executable exemption.
- [ ] Verify current direct and interface link properties.

## Implementation and migration

- [ ] Reuse pending `00` target metadata to record product family.
- [ ] Extend `CheckDependencyBoundaries.cmake`.
- [ ] Reject core-to-product links.
- [ ] Reject cross-product links.
- [ ] Guard checks for disabled optional targets.
- [ ] Keep error messages specific to the violated rule.

## Unit/configure tests

- [ ] Add negative fixtures for core→product and product→other-product edges.
- [ ] Add positive executable-integration and product→core fixtures.
- [ ] Configure all-optionals-off and each optional individually enabled.

## Cross-platform validation

- [ ] Exercise MSVC and Apple generators.
- [ ] Confirm checks are configure-only and introduce no backend behavior.

## Agent documentation/tooling

- [ ] Document family metadata beside the pending `00` helper.
- [ ] Update module-map dependency rules without duplicating target lists.

## Acceptance criteria

- [ ] Prohibited direct/interface edges fail configuration with actionable errors.
- [ ] Optional-disabled configurations remain valid.
- [ ] The executable remains the sole product integration root.
- [ ] Current supported target graph configures on both platforms.

<model>GPT-5 Codex</model>
