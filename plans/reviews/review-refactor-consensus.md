# Refactoring review consensus

Date: 2026-07-21

Scope: only `plans/reviews/review-refactor-latest.*.md`. The Claude and GPT/Codex
reviews contained usable findings; `review-refactor-latest.gemini.md` reports a
failed review and therefore contributes no proposals or attribution.

## Decision summary

The current tree has no new P0 refactor. Claude's proposed P0 App split is real,
but it is already owned by `kanban/ice-box/22 app-workspace-session-controllers -refactor.md`;
duplicating it in `pending` would violate the tracker contract.

Nine proposals are accepted: six P1 boundaries or enabling changes and three P2
isolations. They deliberately avoid reopening the completed broad SatView,
MegaCity, ScoreView, foundation, and modular-test cards. The common direction is
to introduce the smallest static-library or translation-unit boundary that gives
tests and agents an independent ownership lane while retaining compatibility
targets and buildable intermediate commits.

Verification used the current source and CMake files, `docs/module-map.md`,
`docs/features.md`, `CLAUDE.md`, root `AGENTS.md`,
`modules/megacity/AGENTS.md`, `do.py`, `scripts/run_tests.bat`,
`scripts/run_tests.sh`, the live 22-test CTest inventory, and all three Kanban
lanes. `python do.py kanban-report` reported 0 pending, 126 ice-box, and 29 done
cards before the new work items were created.

## Accepted 1: make internal-target build policy local and auditable

**Priority: P1.** This is the prerequisite for every new target below. The
leverage is high and runtime risk is low, but CMake target-type handling must be
correct.

**Current-tree evidence.** Root `CMakeLists.txt:44-112` defines
`draxul_apply_sanitizers`, `draxul_apply_coverage`, and
`draxul_apply_msvc_parallel_pdb_fix`; `CMakeLists.txt:177-237` manually enumerates
targets. Compiled internal targets omitted from those lists include
`draxul-score-learn`, `draxul-score-input`, `draxul-score-audio`,
`draxul-nanovg`, `draxul-renderer-core`, the platform renderer OBJECT target,
and `draxul-vulkan-resources`. Applying flags to the `draxul-renderer` wrapper
does not instrument compilation of its OBJECT libraries.

**Agreed boundary.** Add one CMake helper,
`draxul_configure_internal_target(<target>)`, in project-owned CMake
infrastructure. Each compiled `draxul-*` target opts in next to its definition;
an end-of-configure audit proves every internal target was considered. Imported,
INTERFACE, alias, and explicitly third-party targets are skipped intentionally.

**Interface and dependencies.** This is a CMake-only interface. It adds no C++
API or production link edge. For STATIC, OBJECT, and executable targets it
applies compile policy; link options are applied only to linkable target types.
MSVC `/FS`, macOS/Linux sanitizer/coverage flags, and mutually exclusive
ASan/TSan behavior remain unchanged.

**Migration.** (1) Wrap the existing helpers. (2) Prove one STATIC, one OBJECT,
one executable, and one INTERFACE case. (3) Adopt target-by-target in owning
`CMakeLists.txt` files. (4) add the audit. (5) remove the root lists only after
all optional ON/OFF configurations pass.

**Focused validation enabled.** Configure-time audit tests should fail on an
unconfigured synthetic `draxul-*` target. Narrow commands:

```powershell
cmake --preset default
cmake --build build --config Release --target draxul-renderer-core draxul-tests
python -m unittest tests.do_py_tests
```

Also configure `mac-asan`, `mac-tsan`, and coverage on macOS, and build an MSVC
OBJECT target in parallel on Windows.

**Parity.** No Vulkan/Metal behavior changes, but both backend OBJECT targets
must receive identical project policy. Windows validates `/FS` and ASan;
macOS validates ASan, TSan, and coverage.

**Attribution and ownership.** Proposed by GPT/Codex. One build-system agent
should own the helper, audit, and removal of the root lists. Directory-by-directory
adoption can be delegated only after the helper contract is frozen.

**Dependencies.** None. All other accepted target-creating refactors depend on it.

## Accepted 2: move `draxul-app` build ownership into `app/`

**Priority: P2.** This is a low-risk collision reduction, not a runtime
architecture change.

**Current-tree evidence.** Root `CMakeLists.txt:243-321` selects app platform
sources and defines the 23-source `draxul-app` target, its include path, policy,
and link closure. There is no `app/CMakeLists.txt`; every library and product
family otherwise owns its target locally. Root CMake also owns global options,
dependency order, optional gates, tests, executable packaging, and assets.

**Agreed boundary.** `app/CMakeLists.txt` owns only the `draxul-app` library,
including `app/macos_menu.mm` selection, sources, includes, links, and the local
policy call. Root retains the `draxul` executable, icon/resource packaging,
optional product registration links, install/copy rules, and
`add_subdirectory(app)`.

**Interface and dependencies.** Target name, public `app/` include surface,
compile definitions, and PUBLIC/PRIVATE link relationships remain byte-for-byte
equivalent. Callers remain `draxul`, app tests, and current consumers. No source
file or C++ symbol moves.

**Migration.** (1) Separate app-library and executable platform-source variables.
(2) Move the library block without edits. (3) configure with all optional modules
ON. (4) configure each optional module OFF. (5) compare the generated target
graph and executable assets.

**Focused validation enabled.** App-only build wiring becomes inspectable and
buildable through:

```powershell
cmake --preset default
cmake --build build --config Release --target draxul-app draxul-test-app
ctest --test-dir build -C Release -L app --output-on-failure
```

**Parity.** Preserve Windows `app/draxul.rc` on the executable and macOS
`app/macos_menu.mm` on `draxul-app`; confirm `.app` icon/resource wiring remains
root-owned. Vulkan/Metal selection stays in renderer CMake.

**Attribution and ownership.** Proposed by Claude. One app/build owner should
perform the mechanical move; no sub-agents are useful for a two-file change.

**Dependencies.** Land after Accepted 1 so the new local file uses the final
policy helper.

## Accepted 3: separate host API, grid host, terminal core, and terminal host

**Priority: P1.** This removes a large unnecessary dependency closure from every
non-terminal product and creates high-value test/ownership seams. Migration risk
is moderate because many targets link `draxul-host`.

**Current-tree evidence.** `libs/draxul-host/CMakeLists.txt` puts 14 shared
sources plus ConPTY/Unix PTY sources into one target and PUBLIC-links
`draxul-grid`, `draxul-host-identity`, `draxul-nvim`, `draxul-renderer`,
`draxul-runtime-support`, and `draxul-types`. `host.h` itself includes only
events, host identity, types, GLM, and STL contracts; `grid_host_base.h` adds
grid/rendering pipeline types. `SatViewHost`, `MegaCityHost`, `ScoreHost`, and
Markdown include `host.h` but inherit the full terminal/PTY/Neovim link closure.
Kanban and terminal hosts are the actual `GridHostBase` consumers. VT, SGR,
scrollback, selection, mouse, and key-encoding tests currently link through the
same broad target.

**Agreed boundary.** Introduce, in this order:

- `draxul-host-api`: existing `host.h`, host registry/provider metadata, and
  neutral lifecycle/callback contracts.
- `draxul-grid-host`: `GridHostBase` and grid/cursor/rendering integration.
- `draxul-terminal-core`: VT state/parser, SGR, scrollback, selection,
  alternate-screen, mouse reporting, and terminal key encoding.
- `draxul-terminal-host`: `TerminalHostBase`, local/Nvim hosts, factories, and
  the platform-selected ConPTY or Unix PTY implementation.

Keep `draxul-host` as a compatibility aggregate until every caller migrates.
This is four responsibility layers, not four simultaneous rewrites: each target
must land and migrate independently.

**Interface and dependencies.** Existing header paths, namespaces, `IHost` ABI,
and provider callbacks remain source compatible. `draxul-host-api` publicly
depends only on `draxul-host-identity` and `draxul-types` (plus header-required
GLM transitively). `draxul-grid-host` depends on host API, grid, renderer, and
runtime support. `draxul-terminal-core` depends on grid/types and keeps
performance private. `draxul-terminal-host` depends on grid host, terminal core,
and Nvim transport; OS APIs, window, font, and performance remain private.

**Migration.** (1) Extract registry/API and add link-isolation coverage. (2)
migrate non-grid product hosts, including Markdown. (3) extract `GridHostBase`
and migrate Kanban and terminal callers. (4) move pure terminal sources and their tests.
(5) move concrete/process sources. (6) collapse the compatibility target only
after `rg` shows no old direct consumer.

**Focused validation enabled.** Pure host registry and terminal parser builds no
longer require a PTY or Nvim process:

```powershell
cmake --build build --config Release --target draxul-host-api draxul-terminal-core draxul-test-core
ctest --test-dir build -C Release -L core --output-on-failure
cmake --build build --config Release --target draxul-test-satview draxul-test-megacity draxul-test-scoreview-host
```

Add public-header/link-isolation executables for host API and terminal core.

**Parity.** Keep ConPTY and Unix PTY in one platform-selected terminal-host
target. Exercise Windows process launch/shutdown and macOS fork/exec shutdown.
The API/core cuts are GPU-neutral; product Vulkan/Metal backends must see no
source or scene-contract change.

**Attribution and ownership.** Proposed by GPT/Codex. One host-architecture
owner controls public headers and compatibility migration. After API names are
fixed, a terminal-core test owner may work independently from the platform-host
owner, but they must not edit the same CMake/source lists concurrently.

**Dependencies.** Accepted 1. Accepted 4 and Accepted 5 coordinate their final
links with this boundary.

## Accepted 4: split Neovim protocol/UI from process and RPC transport

**Priority: P1.** It isolates platform-neutral redraw/input/codec work from
thread and child-process work. Risk is moderate because malformed-packet and
shutdown semantics are sensitive.

**Current-tree evidence.** `libs/draxul-nvim/CMakeLists.txt` builds
`nvim_process.cpp`, `mpack_codec.cpp`, `rpc.cpp`, `ui_events.cpp`, and `input.cpp`
as one target. `nvim_rpc.h` combines `NvimProcess` (lines 20-42), `MpackValue`
and message records (46-162), `IRpcChannel` (164-169), and threaded `NvimRpc`
(196-253). `nvim_ui.h` exposes `UiEventHandler` and `NvimInput` against
`MpackValue`/`IRpcChannel`. `mpack_lib` is PUBLIC even though `<mpack.h>` occurs
only in `mpack_codec.cpp`; SDL occurs only in `input.cpp`. Current tests already
separate codec/UI/input files from `nvim_process_tests.cpp`, RPC integration,
and backpressure tests, but they share one link target.

**Agreed boundary.** Add `draxul-nvim-protocol` for `MpackValue`, RPC value
contracts, `IRpcChannel`, codec, redraw decoder, and input encoder. Add
`draxul-nvim-transport` for `NvimProcess`, `NvimRpc`, the reader thread,
request tracking, and platform pipe/process behavior. Keep `draxul-nvim` as a
temporary compatibility aggregate.

**Interface and dependencies.** Preserve names and include paths through
forwarding headers during migration. Contrary to the initial review's overly
narrow dependency sketch, protocol publicly needs both `draxul-types` and the
grid contracts used by `nvim_ui.h`; MPack, SDL, and performance remain PRIVATE.
Transport depends on protocol and types, with performance and OS APIs private.
`UiRequestWorker` and fakes depend on `IRpcChannel`, not transport. `NvimHost`
uses both.

**Migration.** (1) Extract value records and `IRpcChannel`. (2) move codec and
make MPack private. (3) move UI/input. (4) move process/RPC. (5) migrate tests by
responsibility. (6) migrate terminal host and RPC fake. (7) remove forwarding
headers/aggregate only after consumers settle.

**Focused validation enabled.** Codec/redraw/input tests should link protocol
only; fragmentation/backpressure/process tests link transport:

```powershell
cmake --build build --config Release --target draxul-nvim-protocol draxul-nvim-transport draxul-test-core draxul-rpc-fake
ctest --test-dir build -C Release -L core --output-on-failure
```

Add public-header isolation consumers and preserve fake-channel tests.

**Parity.** Protocol behavior must be identical on Windows/macOS. Transport must
preserve MPack extensions, partial reads, reader-thread restrictions, and
non-blocking shutdown on both Windows pipes and POSIX pipes. No Vulkan/Metal
code is involved.

**Attribution and ownership.** Proposed by GPT/Codex. One Nvim owner should
freeze the value/channel headers. Protocol test migration can then proceed
independently from transport/process migration; final `NvimHost` linking is
coordinated with Accepted 3.

**Dependencies.** Accepted 1; final consumer migration depends on Accepted 3's
terminal target names. Source extraction can begin earlier.

## Accepted 5: split Kanban core from grid-host integration

**Priority: P2.** The change is small and mechanically testable, with useful
dependency and ownership reduction.

**Current-tree evidence.** `modules/kanban/draxul-kanban/CMakeLists.txt` places
board, store, layout, navigation, and `kanban_host.cpp` in `draxul-kanban`, then
PUBLIC-links all of `draxul-host`. `KanbanHost` alone inherits `GridHostBase`;
navigation uses SDL privately. `tests/kanban_board_tests.cpp`,
`kanban_store_tests.cpp`, `kanban_layout_tests.cpp`, and
`kanban_navigation_tests.cpp` are pure/core-oriented, while
`kanban_host_tests.cpp` exercises host integration. Markdown already uses
separate core and host targets.

**Agreed boundary.** Retain `draxul-kanban` for board/storage/layout/navigation.
Add `draxul-kanban-host` for `KanbanHost` and provider registration. Do not split
board, store, layout, and navigation into separate static libraries.

**Interface and dependencies.** Core publicly depends on `draxul-types`; SDL is
private for navigation. Host depends on core plus `draxul-grid-host`/host API.
The executable links the host target. Existing public data headers remain in
core; `kanban_host.h` belongs to the host target's public surface.

**Migration.** (1) Add host target without moving directories. (2) move only
`kanban_host.cpp` and its header ownership. (3) update executable/test links.
(4) move pure tests to core-only linkage. (5) add a core header-isolation build.

**Focused validation enabled.**

```powershell
cmake --build build --config Release --target draxul-kanban draxul-kanban-host draxul-test-markdown-kanban
ctest --test-dir build -C Release -L kanban --output-on-failure
```

**Parity.** Filesystem/storage and SDL key behavior must pass on Windows and
macOS. This is grid rendering only and does not change Vulkan/Metal backend code.

**Attribution and ownership.** Proposed by GPT/Codex. One Kanban owner can do
the target and test migration independently after Accepted 3 publishes the grid
target name.

**Dependencies.** Accepted 1 and Accepted 3.

## Accepted 6: create one Megacity semantic model/layout/routing library

**Priority: P1.** This extracts a deterministic, heavily tested domain boundary
from a broad product host closure. The code movement is sizeable, but GPU and
threading risk stay low if the API remains synchronous and immutable.

**Current-tree evidence.** `semantic_city_layout.cpp` is 2,217 lines and contains
`find_grid_path` (line 962), `build_city_routes_from_grid` (1075),
`build_semantic_city_model` (1449), and layout entry points (1745 onward).
`semantic_city_layout.h` is 321 lines and mixes semantic records, layout, grid,
and routes. It is included by city builder/picking/selection, metrics, two UI
panels, the host, and tests. It currently compiles into `draxul-megacity`, whose
PUBLIC closure includes host, renderer, font, config, codeviz renderer/host,
Tree-sitter, GUI, and ImGui. `tests/megacity_scene_layout_tests.cpp` already has
direct deterministic layout/grid/route cases. The nested Megacity guide requires
semantic model construction and spatial layout to remain distinct.

**Agreed boundary and granularity.** Create one static
`draxul-megacity-model`, not four micro-libraries. Inside it, use focused TUs and
headers for semantic model, spatial layout, city grid, and routing. This
reconciles Claude's preference for a private four-way source split with
GPT/Codex's test-isolating static target: one link boundary, several private
implementation responsibilities.

**Interface and dependencies.** Public API contains immutable semantic input,
model/layout/grid/route value records and deterministic build functions. The
current `MegaCityCodeConfig` also contains renderer/UI fields and pulls config,
TOML, and scene types; do not make it the model library's dependency currency.
Introduce a narrow `SemanticCityLayoutOptions` value and adapt from
`MegaCityCodeConfig` in `draxul-megacity`. Public dependencies are limited to
`draxul-code-semantics`, required backend-neutral codeviz scene value types, GLM,
and `draxul-types`; performance is private. Host worker cancellation,
publication, scene construction, renderer, ImGui, SDL, Vulkan, and Metal remain
outside.

**Migration.** (1) Define the narrow options/value API and equivalence tests.
(2) create the target around the current single TU. (3) link the product and
layout tests directly. (4) split model/layout/grid/routing TUs without signature
changes. (5) narrow the broad Megacity test-internals include surface.

**Focused validation enabled.**

```powershell
cmake --build build --config Release --target draxul-megacity-model draxul-test-megacity
ctest --test-dir build -C Release -L megacity --output-on-failure
```

Add direct tests for semantic projection, deterministic lot placement, grid
rasterization, port assignment/pathfinding, and route segment conversion.

**Parity.** The new library is CPU-only and must produce identical scene inputs
for Vulkan and Metal. Validate MegaCity ON/OFF on Windows and macOS. Preserve
deterministic ordering, stable semantic identity, 16-bit geometry-index
assumptions at downstream boundaries, and host-owned cancellation/generation
checks.

**Attribution and ownership.** Proposed independently by Claude and GPT/Codex.
One Megacity model owner controls the public value/options API and target.
After that freezes, layout and routing TU/test moves are safely independent;
neither agent should edit renderer or host lifecycle files.

**Dependencies.** Accepted 1. Independent of Accepted 3 after the target is
created because it must not link host APIs.

## Accepted 7: decompose SatView host around scene composition and interaction

**Priority: P1.** This is the largest current agent-collision hotspot and has
existing CPU-only test seams. Risk is controlled by reusing current targets and
not changing the renderer scene ABI.

**Current-tree evidence.** `satview_host.cpp` is 5,317 lines and
`satview_host.h` is 298 lines. The TU contains camera/projection helpers
(lines 142-339), colors/filtering and scene builders (339-958 and 2435-2698),
`SatViewHost::draw` (1283), config mapping (2251), object-tree/panels
(3171-4759), and selection (4956-5317). The existing target graph is already
sound: core -> scene/services -> renderer -> host, completed by
`kanban/done/26 satview-library-boundaries -refactor.md`.
`draxul-satview-scene` already owns backend-neutral POD/scene helpers;
`draxul-satview-host-test-internals` and the offline fixture provide fake clock,
network, renderer, and callbacks.

**Agreed boundary.** Add `SatViewSceneComposer` to the existing
`draxul-satview-scene`; it consumes immutable simulation/filter/view inputs and
returns backend-neutral vertices, markers, labels, visible radius, and dirty
revisions. Add a private host-side view/selection controller for POV, map/ground
interaction, and selection transitions. Move ImGui methods to private panel TUs.
`SatViewHost` retains service/worker lifetime, publication, renderer attachment,
frame scheduling, and collaborator composition. Do not create another static
library or reopen the completed broad boundary card.

**Interface and dependencies.** Scene composer public inputs/results use
SatView core plus existing scene POD contracts and must not include HTTP, ImGui,
SDL, Vulkan, or Metal. The view controller and panels are implementation-private
to the host target. The renderer continues to consume the unchanged
`SatViewScenePass` contract.

**Migration.** (1) Move pure anonymous helpers with output-equivalence tests.
(2) introduce one request/result for scene composition and preserve generation/
dirty behavior. (3) move picking and camera transitions. (4) move one ImGui
window at a time. (5) shrink concrete-host private declarations only after all
callers migrate.

**Focused validation enabled.**

```powershell
cmake --build build --config Release --target draxul-satview-scene draxul-test-satview
ctest --test-dir build -C Release -L satview --output-on-failure
```

Add device-free composer tests for track/marker limits, selection emphasis,
projection modes, visible bounds, and dirty revisions; controller tests for POV,
map drag, ground entry, and selection; retain offline host smoke.

**Parity.** Keep the scene ABI and dirty-buffer semantics identical for Vulkan
and Metal. Configure SatView ON/OFF on Windows and macOS, build both backend
paths, and preserve fake-clock/offline-service behavior. No GPU resource may
move into the composer.

**Attribution and ownership.** Proposed independently by Claude and GPT/Codex.
One SatView boundary owner freezes composer request/result and state ownership.
Afterward, scene-composer tests and panel TU moves can be independent; renderer
backend files remain out of scope.

**Dependencies.** Accepted 1. Independent of host/Nvim/Kanban work.

## Accepted 8: split ScoreView analysis-overlay construction from NanoVG drawing

**Priority: P2.** This is a narrow, evidenced test-isolation improvement with no
behavioral redesign.

**Current-tree evidence.** `analysis_overlay.cpp` is 629 lines:
`build_analysis_overlay` begins at line 46 and NanoVG draw functions begin at
413. The header explicitly says build is pure and draw replays through NanoVG.
Nevertheless the entire source is compiled into `draxul-scoreview-host`, and
`scoreview_overlay_tests.cpp` is explicitly assigned to
`DRAXUL_SCOREVIEW_HOST_TEST_SOURCES`, forcing the full host/NanoVG/SDL/ImGui link
closure for geometry assertions. `draxul-scoreview` is the existing model/layout
target and `draxul-test-scoreview` is the existing lighter test target.

**Agreed boundary.** Put `AnalysisOverlay` values and
`build_analysis_overlay` in `analysis_overlay_build.cpp` owned by
`draxul-scoreview`. Put NanoVG replay in `analysis_overlay_draw.cpp` plus an
implementation-private draw header owned by `draxul-scoreview-host`. Do not add
a new static library.

**Interface and dependencies.** The current public analysis value/build API
remains source compatible. Core callers depend on ScoreView draw-list, timemap,
highlight, and learning profile types only. The host-private drawing interface
depends on NanoVG and `ScoreTextFonts`. `ScoreHost` and `ScorePresentation` are
the production callers.

**Migration.** (1) Split declarations without behavior changes. (2) move pure
implementation to core target. (3) move draw implementation to host. (4) move
overlay tests to core test sources. (5) retain one host-level replay smoke case
if draw-specific coverage exists.

**Focused validation enabled.**

```powershell
cmake --build build --config Release --target draxul-scoreview draxul-test-scoreview draxul-test-scoreview-host
ctest --test-dir build -C Release -L scoreview --output-on-failure
```

**Parity.** Overlay construction is platform/GPU neutral. NanoVG replay must
remain identical on Windows and macOS; Vulkan/Metal renderer APIs are unchanged.
Preserve the macOS microphone/TCC ordering by not touching audio/input code.

**Attribution and ownership.** Proposed by Claude. One ScoreView owner can make
the split; core tests and host draw smoke are separable after declarations settle.

**Dependencies.** Accepted 1 only. It must not reopen
`kanban/done/21 scoreview-host-decomposition -refactor.md`.

## Accepted 9: repair scoped agent guidance and add stable label-level tests

**Priority: P1.** Stale commands and architecture claims directly cause agents
to run invalid validation or reason from deleted interfaces. Runtime risk is low;
workflow leverage is high.

**Current-tree evidence.** `CLAUDE.md:129-130` still documents deleted
`I3DHost`/`IGridHost` and `attach_3d_renderer`; current `host.h` contains one
`IHost` interface and `GridHostBase` is a separate concrete base header.
`CLAUDE.md` sanitizer examples use `ctest -R draxul-tests`, but the live inventory
contains `draxul-test-<area>-shard-N`. `modules/megacity/AGENTS.md` tells agents
to run a nonexistent `draxul-tests.exe` and names deleted
`megacity_scene_tests.cpp`. `do.py`/README still say four C++ shards while the
current CMake creates core (4), app (2), Markdown/Kanban (1), MegaCity (2),
SatView (2), ScoreView (2), and ScoreView-host (1) shards. `do.py test` accepts
no label and the platform scripts only support all-unit mode. SatView and Score
have complex optional target families but no nested `AGENTS.md`.

**Agreed boundary.** Keep shared rules canonical in `CLAUDE.md` and root
`AGENTS.md` as a thin pointer. Correct current host/test architecture and counts.
Update Megacity's nested guide. Add concise `modules/satview/AGENTS.md` and
`modules/score/AGENTS.md`, with root pointers. Add a stable
`python do.py test --label <label>` entry point that maps a known CTest label to
the smallest owning build target before running CTest. The authoritative mapping
comes from the focused targets/labels in `tests/CMakeLists.txt`, not duplicated
hard-coded shard counts in prose.

**Interface and dependencies.** This is documentation plus Python/shell/batch
tooling. Supported initial labels should cover `core`, `app`, `session`,
`markdown`, `kanban`, `megacity`, `satview`, `scoreview`, and
`scoreview-host`; optional-disabled labels report a clear unavailable result.
No production dependency changes.

**Migration.** (1) Correct deleted symbols, files, and commands. (2) implement
and unit-test label parsing/target mapping. (3) teach both platform wrappers the
same build-target/CTest-label contract or have `do.py` call CMake/CTest directly.
(4) add local guides. (5) add a lightweight docs check for named current files/
targets where practical. (6) update README/help without hard-coded shard totals.

**Focused validation enabled.**

```powershell
python -m unittest tests.do_py_tests
python do.py test --label megacity
python do.py test --label satview
python do.py test --label scoreview-host
```

**Parity.** The command must construct correct multi-config Windows and
single-config macOS invocations. Local guides must state Vulkan/Metal parity,
optional ON/OFF builds, worker lifetime, generated assets, and Score's macOS TCC
ordering where relevant.

**Attribution and ownership.** Proposed by GPT/Codex; missing SatView/Score
guides were also proposed by Claude. One guidance/tooling owner should update
canonical docs and `do.py`; SatView and Score guide drafts can be independent
only after final target names from Accepted 3-8 are stable.

**Dependencies.** Land last, after Accepted 3-8, so it documents final names.
It is intentionally separate from
`kanban/ice-box/22 agent-scripts-deduplication-refactor.md`, which concerns AI
review script code.

## Rejected, deferred, or already owned findings

| Proposal | Decision |
|---|---|
| App `WorkspaceManager`/`SessionCoordinator` (Claude P0) | Valid but already tracked by `kanban/ice-box/22 app-workspace-session-controllers -refactor.md`, including ownership, focus, rollback, tests, and sequencing. Do not duplicate. |
| Broad renderer `State` decomposition and cross-backend pipeline manifest (Claude) | Not accepted as one card: rewrite-scale and high parity risk. MegaCity host/renderer decomposition is completed in `kanban/done/27 megacity-host-renderer-decomposition -refactor.md`; shared GPU helpers in `kanban/done/28 shared-gpu-resource-helpers -refactor.md`; remaining backend cleanup is `kanban/ice-box/25 renderer-backend-parity-cleanup -refactor.md`; shader ABI parity is `kanban/ice-box/19 shader-abi-parity -test.md`. New measured subproblems should update those cards. |
| Uniform re-decomposition of SatView, MegaCity, and Score hosts (Claude) | Partly stale. Score host decomposition is `kanban/done/21 scoreview-host-decomposition -refactor.md` and MegaCity is `kanban/done/27 megacity-host-renderer-decomposition -refactor.md`. Only the narrower SatView composer/controller/panel follow-on is accepted. |
| Four Megacity static libraries for model/layout/grid/routing | Rejected granularity. The algorithms need separate TUs and tests, but one `draxul-megacity-model` link boundary is sufficient and keeps CMake/optional-gate cost bounded. |
| SatView JSON/CSV parser, Score SVG parser, and per-analyzer TU splits (Claude) | Deferred: current evidence shows file-local cohesion but no second caller, independent target, or blocked test seam. File size alone is not a boundary. |
| Split `config_schema.cpp` (Claude) | Dropped: the current table-driven registry intentionally keeps parse/serialize/docs behavior synchronized; the review itself calls it cohesive. |
| Make `SatViewConfig` the sole live host state (Claude) | Deferred as speculative and behavior-sensitive. The accepted host decomposition should first expose actual ownership and dirty-generation contracts. |
| Move Score learning policy and local duplicate helpers (Claude) | Deferred: useful cleanup, but either behavior-sensitive or local deduplication without a new test/link boundary. Reassess within the existing controller owners, not as consensus cards. |
| Split `ui_treesitter_panel.cpp` solely by panel (Claude) | Dropped as a mechanical file-size suggestion after completed MegaCity host collaborator extraction. Accept only when a stable model/side-effect interface is demonstrated. |
| Consolidate SatView renderer asset paths (Claude) | Stale decision: `kanban/done/26 satview-library-boundaries -refactor.md` explicitly evaluated the shared-contract option and deliberately retained SatView-local `resolve_satview_asset_path` to avoid product coupling. |
| Reopen broad test modularization (implicit in reviews) | Already completed by `kanban/done/35 modular-test-targets -refactor.md`. Accepted cards extend existing focused targets/labels rather than replace them. |

## Final target/module map

Arrows point from caller to dependency. Compatibility aggregates are temporary.

```text
draxul executable
  -> draxul-app                         (definition moves to app/CMakeLists.txt)
  -> draxul-markdown-host -> draxul-markdown + draxul-host-api
  -> draxul-kanban-host -> draxul-kanban
  -> draxul-satview-host
       -> draxul-satview-services
       -> draxul-satview-scene          (adds SatViewSceneComposer)
       -> draxul-satview-core
       -> draxul-satview-renderer
  -> draxul-megacity
       -> draxul-megacity-model         (model/layout/grid/routing)
            -> draxul-code-semantics
            -> backend-neutral draxul-codeviz-scene values
       -> existing codeviz host/scene/renderer targets
  -> draxul-scoreview-host
       -> draxul-scoreview              (owns analysis-overlay construction)
       -> host-private NanoVG overlay drawing

draxul-host-api
  <- non-grid product hosts
draxul-grid-host -> draxul-host-api + grid/renderer/runtime support
  <- draxul-kanban-host and other grid hosts
draxul-terminal-core -> grid + types
draxul-terminal-host -> draxul-grid-host + draxul-terminal-core
  -> draxul-nvim-transport -> draxul-nvim-protocol
draxul-nvim-protocol -> types + grid contracts

temporary compatibility: draxul-host, draxul-nvim
all compiled internal targets -> draxul_configure_internal_target(target)
```

No product target may point back into `app/`; CPU model/composer targets may not
link host, ImGui, SDL, or a GPU backend unless their public contract requires it.

## Repository guidance and agent-oriented tooling

- Root `AGENTS.md` should remain a short Codex pointer to `CLAUDE.md`, with
  explicit nested-guide pointers for `modules/megacity/`, `modules/satview/`,
  and `modules/score/`. Shared rules must not be copied into each guide.
- `CLAUDE.md` must describe current `IHost`/`GridHostBase` ownership, current
  focused CTest executables/labels, compatibility aggregates during migration,
  and the final dependency map. Avoid hard-coded shard totals.
- Megacity's guide must name `draxul-test-megacity`, `ctest -L megacity`, and
  current focused test files such as `megacity_scene_layout_tests.cpp`,
  `megacity_scene_host_tests.cpp`, and `megacity_scene_world_tests.cpp`.
- New SatView guidance should record core/scene/services/renderer/host direction,
  immutable worker publication, fake-clock/offline tests, asset generation, ON/OFF
  builds, and paired Vulkan/Metal scene-ABI checks.
- New Score guidance should record notation/learn/input/audio/model/host direction,
  GPU/transport-free learning rules, async engrave lifetime, generated assets,
  focused core/host labels, and macOS microphone-consent ordering.
- `python do.py test --label <label>` should be the stable bounded-validation
  entry point. `tests/CMakeLists.txt` remains authoritative; Python tests verify
  parsing, target selection, optional-disabled behavior, and Windows/macOS command
  construction.
- Add link-isolation executables for each new public static boundary and keep
  `python do.py kanban-report`, `python do.py hygiene`, full build, CTest, and
  smoke as closeout gates where their scope applies.

## Recommended sequencing and interdependencies

1. Land Accepted 1 (build policy) first.
2. Land Accepted 2 (app-local CMake) next so subsequent app additions avoid root
   CMake; it can overlap product-only design work but not another root-CMake edit.
3. Land Accepted 3 host API first, then grid host, terminal core, and terminal
   host as separately green migrations.
4. Begin Accepted 4 protocol extraction after Accepted 1; coordinate its final
   `NvimHost` link with Accepted 3's terminal-host phase.
5. Land Accepted 5 after `draxul-grid-host` exists.
6. Accepted 6, 7, and 8 can proceed independently after Accepted 1 because they
   occupy Megacity, SatView, and Score directories respectively. Freeze each
   value/request interface before delegating TU or test moves.
7. Land Accepted 9 last so guidance and label mappings name final targets.
8. Run full build plus `python do.py smoke` after all target migrations; validate
   optional module OFF configurations and record any platform runtime coverage not
   available locally.

Implementation work items, in sequence:

1. `kanban/pending/00 internal-target-build-policy -refactor.md`
2. `kanban/pending/01 app-local-cmake-ownership -refactor.md`
3. `kanban/pending/02 host-layer-static-libraries -refactor.md`
4. `kanban/pending/03 nvim-protocol-transport-boundaries -refactor.md`
5. `kanban/pending/04 kanban-core-host-boundary -refactor.md`
6. `kanban/pending/05 megacity-model-layout-routing-library -refactor.md`
7. `kanban/pending/06 satview-scene-composer-host-split -refactor.md`
8. `kanban/pending/07 scoreview-analysis-overlay-boundary -refactor.md`
9. `kanban/pending/08 agent-guidance-label-validation -refactor.md`

<model>GPT-5 Codex</model>
