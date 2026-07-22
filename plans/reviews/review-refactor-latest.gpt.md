# Draxul structural refactor review

## Executive summary

The repository is already materially modular: optional products are gated, renderer backends are private, SatView and ScoreView have meaningful library layers, and tests are partitioned into focused CTest targets. No P0 issue currently blocks safe change.

The remaining review produced **six P1 findings and one P2 finding**. The strongest opportunities are:

1. Separate the host contract from terminal/PTY implementation.
2. Make internal-target build policies local and automatic.
3. Decompose the 5,300-line SatView host around existing scene, interaction, and panel seams.
4. Split Neovim protocol logic from process/RPC transport.
5. Extract Megacity’s pure semantic model/layout/routing code into its own static target.
6. Repair agent guidance and expose reliable label-level validation.
7. Split Kanban’s pure model from its host integration.

No files were modified. The worktree remained clean. I used the live files and CMake definitions, not a combined source artifact. The only command-level validation was read-only CTest inventory; no application or test executable was launched.

## Tracker exclusions

I did not re-report work already owned by the tracker, including:

- App workspace/session controllers.
- Overlay registry.
- `UiRequestWorker` evaluation.
- Renderer backend residual cleanup.
- Agent-script deduplication.
- Shader ABI parity.
- Core/extras repository separation and its consolidated registration seam.
- Completed SatView library boundaries.
- Completed Megacity host/renderer decomposition.
- Completed modular test-target work.
- Completed foundation dependency cleanup.

---

## 1. Separate the host contract from grid and terminal implementations

**Priority:** P1

**Location**

- [`libs/draxul-host/CMakeLists.txt:1-44`](/D:/dev/Draxul/libs/draxul-host/CMakeLists.txt:1)
- [`libs/draxul-host/include/draxul/host.h:158-296`](/D:/dev/Draxul/libs/draxul-host/include/draxul/host.h:158)
- [`libs/draxul-host/include/draxul/host_registry.h:28`](/D:/dev/Draxul/libs/draxul-host/include/draxul/host_registry.h:28)
- [`tests/CMakeLists.txt:242-264`](/D:/dev/Draxul/tests/CMakeLists.txt:242)

**Current structural problem**

`draxul-host` currently owns four distinct layers:

- The neutral `IHost`, `HostContext`, callbacks, and provider registry.
- `GridHostBase`.
- Pure terminal state/parsing: VT parser, SGR, scrollback, selection, key encoding, mouse reporting, and alternate-screen state.
- Concrete Neovim/shell hosts and Windows ConPTY or Unix PTY processes.

Its PUBLIC dependency closure includes grid, host identity, Neovim, renderer, runtime support, and types. Yet `host.h` itself only includes event/value contracts and forward-declares window, renderer, font, and configuration objects.

Consequently, non-terminal product hosts such as SatView, ScoreView, and Megacity depend on the complete terminal/PTY/Neovim target merely to implement `IHost`. Pure terminal parser tests also inherit the concrete host dependency graph.

**Proposed boundary**

Introduce these narrow targets incrementally:

- `draxul-host-api`: `host.h`, provider metadata/registry, and neutral host lifecycle contracts.
- `draxul-grid-host`: `GridHostBase` and the grid rendering/cursor integration.
- `draxul-terminal-core`: VT parser/state, SGR, scrollback, selection, alternate screen, mouse reporting, and terminal key encoding.
- `draxul-terminal-host`: `TerminalHostBase`, `LocalTerminalHost`, `NvimHost`, shell factories, and the platform PTY implementation.

Keep `draxul-host` temporarily as a compatibility aggregate.

**Dependency shape**

- `draxul-host-api` → `draxul-host-identity`, `draxul-types`.
- `draxul-grid-host` → host API, grid, renderer/runtime support; font/performance private.
- `draxul-terminal-core` → grid and types; performance private.
- `draxul-terminal-host` → grid host, terminal core, Neovim transport; window/font and OS libraries private.
- Non-grid products → host API only.
- Kanban and terminal products → grid host.
- Shell/Neovim products → terminal host.

**Migration path**

1. Move the existing provider registry implementation and neutral headers to `draxul-host-api` without changing namespaces or include paths.
2. Make `draxul-host` link the new target so current consumers remain buildable.
3. Migrate SatView, ScoreView, Megacity, and Markdown to the API target.
4. Move `GridHostBase` into its own target and migrate Kanban/terminal hosts.
5. Extract the pure terminal sources.
6. Leave platform process files in `draxul-terminal-host`; remove the compatibility aggregate only after all consumers migrate.

**Testing improvement**

- Add public-header/link-isolation consumers for `draxul-host-api` and `draxul-terminal-core`.
- Run parser, SGR, scrollback, selection, mouse, and key-encoding tests without PTY, renderer, or Neovim process dependencies.
- Keep platform host lifecycle and RPC integration tests against `draxul-terminal-host`.

**Agent-work benefit**

Terminal parser, provider-registry, grid-host, and platform-process changes receive separate ownership. A product module can implement an `IHost` without loading terminal and transport implementation context.

**Risks and prerequisites**

- Preserve the exact `IHost` ABI and callback lifetime rules during the first migration.
- Keep ConPTY and Unix PTY sources in one platform-selected target so Windows/macOS behavior does not diverge.
- Coordinate target renames with the core/extras work item, but do not absorb its already-tracked registration design.

---

## 2. Make internal-target build policies local and mechanically enforced

**Priority:** P1

**Location**

- [`CMakeLists.txt:43-112`](/D:/dev/Draxul/CMakeLists.txt:43)
- [`CMakeLists.txt:177-237`](/D:/dev/Draxul/CMakeLists.txt:177)
- Representative target definitions throughout `libs/*/CMakeLists.txt` and `modules/*/CMakeLists.txt`.

**Current structural problem**

Sanitizer, coverage, and MSVC parallel-PDB policies are applied from manually maintained lists in the root CMake file. Adding a library requires editing both its owning directory and this central list.

The current list omits compiled project targets including:

- `draxul-score-learn`
- `draxul-score-input`
- `draxul-score-audio`
- `draxul-nanovg`
- `draxul-renderer-core`
- `draxul-renderer-metal` / `draxul-renderer-vulkan`
- `draxul-vulkan-resources`

Applying policy only to the final `draxul-renderer` wrapper does not instrument compilation of its OBJECT libraries. The design also turns root CMake into a recurring collision point for otherwise isolated target additions.

**Proposed boundary**

Add one project-policy function, for example:

```cmake
draxul_configure_internal_target(target)
```

It should:

- Inspect target type.
- Apply sanitizer and coverage compile options to compiled targets.
- Apply link options only where the target type supports linking.
- Apply the MSVC `/FS` policy.
- Mark the target with a property proving the policy was considered.
- Skip imported, interface-only, and deliberately third-party targets explicitly.

Call it next to each internal target definition.

**Dependency shape**

This is build-policy infrastructure only; it adds no production link edge or public C++ API.

**Migration path**

1. Implement the helper using the existing three functions.
2. Adopt it in one leaf library and one OBJECT library.
3. Convert core libraries directory by directory.
4. Convert optional products.
5. Add a configure-time audit over internal targets.
6. Remove the root enumeration after the audit passes.

**Testing improvement**

Configure-time checks can fail if a new compiled `draxul-*` target has not declared its policy. Validate with:

- Windows MSVC configuration, including parallel target builds.
- macOS ASan, TSan, and coverage presets.
- One target that is INTERFACE, one STATIC, one OBJECT, and one executable.

**Agent-work benefit**

An agent adding a cohesive target edits its own directory only. The target cannot silently miss sanitizer/coverage behavior, and unrelated module work stops colliding in root CMake.

**Risks and prerequisites**

- OBJECT libraries need compile instrumentation but not independent link options.
- Avoid applying project flags to FetchContent or externally maintained targets.
- Land this before introducing the new targets proposed elsewhere in this review.

---

## 3. Decompose `SatViewHost` around scene composition, interaction, and panels

**Priority:** P1

**Location**

- [`satview_host.cpp:59-958`](/D:/dev/Draxul/modules/satview/draxul-satview-host/src/satview_host.cpp:59)
- [`satview_host.cpp:1283`](/D:/dev/Draxul/modules/satview/draxul-satview-host/src/satview_host.cpp:1283), `SatViewHost::draw`
- [`satview_host.cpp:2251`](/D:/dev/Draxul/modules/satview/draxul-satview-host/src/satview_host.cpp:2251), configuration/simulation synchronization
- [`satview_host.cpp:3171`](/D:/dev/Draxul/modules/satview/draxul-satview-host/src/satview_host.cpp:3171), object-tree and ImGui UI
- [`satview_host.cpp:4810`](/D:/dev/Draxul/modules/satview/draxul-satview-host/src/satview_host.cpp:4810), selection
- [`satview_host.h:77-292`](/D:/dev/Draxul/modules/satview/draxul-satview-host/include/draxul/satview/satview_host.h:77)
- [`draxul-satview-host/CMakeLists.txt:13-38`](/D:/dev/Draxul/modules/satview/draxul-satview-host/CMakeLists.txt:13)

**Current structural problem**

`SatViewHost` is approximately 5,300 source lines with nearly all mutable product state in one class. Its translation unit contains:

- Pure camera/projection and picking math.
- CPU construction of tracks, markers, celestial lines, and scene radii.
- Host/service lifecycle and simulation polling.
- Frame uniform and scene snapshot assembly.
- Mouse, keyboard, and camera manipulation.
- Config projection and dirty-state synchronization.
- Text-atlas and label layout.
- Several thousand lines of ImGui panels and object trees.
- Satellite, surface-object, and natural-body selection.

The completed SatView target split correctly separates core, services, scene, renderer, and host, but much scene-composition logic still lives above the scene boundary. Unrelated panel, picking, camera, and scene-data work all collide in the same file and class.

**Proposed boundary**

Use the existing targets rather than adding generic abstractions:

- Add `satview_scene_composer.{h,cpp}` to `draxul-satview-scene`. It should consume immutable simulation/filter/view inputs and return backend-neutral vertices, markers, labels, visible-radius data, and dirty revisions.
- Add a private `SatViewViewController` or equivalent to own camera POV, map/ground interaction, and selection transitions.
- Move ImGui methods into `satview_host_panels.cpp`; keep them private to `draxul-satview-host`.
- Leave `SatViewHost` responsible for service lifetime, worker publication, renderer attachment, frame scheduling, and composition of collaborators.

**Dependency shape**

- Host → services, view controller, scene composer, scene pass.
- Scene composer → SatView core and scene POD contracts; no HTTP, ImGui, SDL, Vulkan, or Metal.
- View controller → core math, host events, and small view-state records; no GPU backend.
- Panels → ImGui plus controller/config view models; implementation-private.
- Renderer continues consuming the unchanged backend-neutral scene contract.

**Migration path**

1. Move anonymous pure helpers from the first 1,000 lines into the scene target and pin output equivalence with tests.
2. Replace direct vector construction in `draw()` with one scene-composer request/result.
3. Move object selection and camera/ground transitions behind the controller.
4. Move panel methods one window at a time to the private panel TU.
5. Shrink the public concrete host header or make it implementation-private after the registration facade work is resolved elsewhere.

**Testing improvement**

- Pure scene-composition tests can cover track/marker limits, selection emphasis, projection variants, and visible bounds without constructing `SatViewHost`.
- Controller tests can exercise camera POV, map dragging, ground entry, and selection transitions without ImGui or a renderer.
- Existing offline host smoke tests remain the lifecycle/integration gate.
- Both render backends continue to consume one tested scene result.

**Agent-work benefit**

Scene construction, camera/selection, panels, services, and renderer work become separate file ownership lanes. Most SatView changes would no longer require editing the dominant hotspot.

**Risks and prerequisites**

- Preserve dirty-buffer and generation semantics; do not make the composer own GPU resources.
- Keep the renderer scene ABI identical on Vulkan and Metal.
- Preserve the fake-clock/offline-service seam used by existing host smoke tests.
- Do not reopen the already-completed broad SatView library-boundary card.

---

## 4. Split Neovim protocol/UI logic from process and threaded RPC transport

**Priority:** P1

**Location**

- [`libs/draxul-nvim/CMakeLists.txt:1-12`](/D:/dev/Draxul/libs/draxul-nvim/CMakeLists.txt:1)
- [`nvim_rpc.h:18-42`](/D:/dev/Draxul/libs/draxul-nvim/include/draxul/nvim_rpc.h:18), process API
- [`nvim_rpc.h:44-194`](/D:/dev/Draxul/libs/draxul-nvim/include/draxul/nvim_rpc.h:44), value/channel protocol
- [`nvim_rpc.h:196-253`](/D:/dev/Draxul/libs/draxul-nvim/include/draxul/nvim_rpc.h:196), threaded RPC client
- [`nvim_ui.h:15-145`](/D:/dev/Draxul/libs/draxul-nvim/include/draxul/nvim_ui.h:15)

**Current structural problem**

One target and one principal header combine:

- Cross-platform child-process lifecycle.
- `MpackValue` and RPC message contracts.
- MPack encoding/decoding.
- Threaded request/response transport and notification queuing.
- UI redraw decoding.
- Keyboard, text, and mouse encoding.

`mpack_lib` is PUBLIC even though no MPack C type appears in the public headers. Tests for input or redraw decoding therefore depend on the process/RPC implementation and external codec include surface.

**Proposed boundary**

- `draxul-nvim-protocol`: `MpackValue`, `IRpcChannel`, codec, UI event decoder, and input encoder.
- `draxul-nvim-transport`: `NvimProcess`, `NvimRpc`, reader thread, request tracking, and OS pipe/process behavior.
- Keep `draxul-nvim` as a compatibility aggregate during migration.

Do not generalize `IRpcChannel` beyond the demonstrated Neovim contract.

**Dependency shape**

- Protocol → `draxul-types`; MPack and SDL private to the relevant implementation files.
- Transport → protocol and performance; OS process APIs private.
- `NvimHost` → transport plus protocol.
- `UiRequestWorker` → protocol’s `IRpcChannel`, not process transport.
- Pure UI/input tests → protocol only.

**Migration path**

1. Extract `MpackValue`, message records, and `IRpcChannel` into a protocol header without changing names.
2. Move codec, UI-event, and input sources into the protocol target.
3. Move process/RPC classes into transport-specific headers and target.
4. Retain forwarding convenience headers during one migration window if needed.
5. Make `mpack_lib` private and add a public-header isolation build.
6. Update the terminal host after the host-boundary target names settle.

**Testing improvement**

- Codec fuzzing, redraw parsing, and input encoding can build without child-process code.
- RPC queue/backpressure and fragmentation tests remain transport tests.
- The fake RPC channel becomes the standard seam for UI/input tests.
- Add standalone public-header consumers for protocol and transport.

**Agent-work benefit**

UI protocol changes stop colliding with pipe/thread/process changes. Windows process work and platform-neutral redraw/input work can proceed independently.

**Risks and prerequisites**

- Preserve MPack extension handling and malformed/truncated packet behavior.
- Keep reader-thread callbacks and main-thread request restrictions unchanged.
- Validate process shutdown on Windows and macOS after the split.
- Coordinate the final link change with the host-boundary work, but source moves are otherwise independent.

---

## 5. Extract Megacity semantic model, spatial layout, and routing into a static target

**Priority:** P1

**Location**

- [`semantic_city_layout.h:19-319`](/D:/dev/Draxul/modules/megacity/draxul-megacity/src/semantic_city_layout.h:19)
- [`semantic_city_layout.cpp:114`](/D:/dev/Draxul/modules/megacity/draxul-megacity/src/semantic_city_layout.cpp:114), lot placement
- [`semantic_city_layout.cpp:423`](/D:/dev/Draxul/modules/megacity/draxul-megacity/src/semantic_city_layout.cpp:423), route ports/grid pathfinding
- [`semantic_city_layout.cpp:1449`](/D:/dev/Draxul/modules/megacity/draxul-megacity/src/semantic_city_layout.cpp:1449), semantic model construction
- [`semantic_city_layout.cpp:1745`](/D:/dev/Draxul/modules/megacity/draxul-megacity/src/semantic_city_layout.cpp:1745), layout
- [`semantic_city_layout.cpp:2020`](/D:/dev/Draxul/modules/megacity/draxul-megacity/src/semantic_city_layout.cpp:2020), grid/routing API
- [`draxul-megacity/CMakeLists.txt:3-53`](/D:/dev/Draxul/modules/megacity/draxul-megacity/CMakeLists.txt:3)

**Current structural problem**

`semantic_city_layout.cpp` is over 2,100 lines and combines three independently testable algorithms:

- Projection of semantic records into building/module models.
- Lot packing and module/city spatial layout.
- Occupancy-grid construction, A*-style routing, route-port assignment, and render-segment conversion.

The code is backend-neutral and already heavily tested, but it compiles inside `draxul-megacity`, whose dependency closure includes host, renderer, font, config, scene renderer, ImGui, UI, and geometry. Ten production/test files include its private header through the broad Megacity internal-test surface.

The local Megacity guide explicitly says semantic model construction and spatial layout are distinct, but the current target/file boundary does not express that rule.

**Proposed boundary**

Create `draxul-megacity-model` as a small static library with:

- `semantic_city_model.cpp`
- `semantic_city_layout.cpp`
- `city_grid.cpp`
- `city_routing.cpp`

Its public API should contain only immutable semantic model/layout/grid records and the existing deterministic build functions. Lot grids, route-port assignment, pathfinding queues, and simplification helpers remain private.

**Dependency shape**

- Callers: `draxul-megacity`, city builder, picking/selection, metrics/UI panels, and focused tests.
- Public dependencies: code-semantics records, neutral scene component/value types, GLM/config value contracts.
- Private dependencies: performance instrumentation.
- No host, renderer backend, ImGui, SDL, font, Vulkan, or Metal dependency.
- `draxul-megacity` remains responsible for worker cancellation, publication, scene construction, and UI.

**Migration path**

1. Move the current header into an explicit module-owned include surface.
2. Add the new target initially with the current single implementation file.
3. Link `draxul-megacity` against it and move existing tests to direct linkage.
4. Split model construction, layout, and routing into separate TUs without changing signatures.
5. Narrow the Megacity white-box test interface once layout tests no longer need the entire `src/` include directory.

**Testing improvement**

`megacity_scene_layout_tests.cpp` can link only the new static library and run model/layout/routing cases without the host or GPU stack. Separate fixtures can pin:

- Semantic projection.
- Deterministic lot placement.
- Grid rasterization.
- Port assignment and pathfinding.
- Route render-segment construction.

**Agent-work benefit**

Semantic projection, spatial packing, routing, renderer, and host lifecycle gain distinct ownership. Layout work stops recompiling or conflicting with the large product host target.

**Risks and prerequisites**

- Decide ownership of `MegaCityCodeConfig`: either move its value contract into the model target or create a small config target; do not add a reverse dependency on `draxul-megacity`.
- Preserve deterministic ordering and stable semantic identity.
- Keep cancellation and generation checks in the host; the model library should remain synchronous and pure.
- This is narrower than, and does not reopen, the completed host/renderer decomposition card.

---

## 6. Repair agent guidance and expose current label-level validation commands

**Priority:** P1

**Location**

- [`CLAUDE.md:129-130`](/D:/dev/Draxul/CLAUDE.md:129)
- [`CLAUDE.md:54-61`](/D:/dev/Draxul/CLAUDE.md:54)
- [`modules/megacity/AGENTS.md:65-83`](/D:/dev/Draxul/modules/megacity/AGENTS.md:65)
- [`tests/CMakeLists.txt:201-343`](/D:/dev/Draxul/tests/CMakeLists.txt:201)
- [`do.py:1684-1717`](/D:/dev/Draxul/do.py:1684)
- [`do.py:1786-1789`](/D:/dev/Draxul/do.py:1786)

**Current structural problem**

The canonical and nested guidance has drifted behind current source/CMake:

- `CLAUDE.md` describes `IHost → I3DHost → IGridHost → GridHostBase` and `attach_3d_renderer()`. The current `host.h` contains only `IHost`; those intermediate types and hook no longer exist.
- The sanitizer examples use `ctest -R draxul-tests`, but current tests are named `draxul-test-<module>-shard-N`.
- The Megacity guide tells agents to run `build/Release/draxul-tests.exe`. `draxul-tests` is now a custom aggregate target, not an executable.
- The guide points to deleted `megacity_scene_tests.cpp`; current coverage is split across host, layout, and world files.
- `do.py` help and other prose still describe four C++ shards, while CMake now creates seven focused executables and multiple shards.
- SatView and ScoreView have five-target families, worker/lifetime rules, generated assets, and backend parity requirements but no local `AGENTS.md` or README.

A live `ctest -N -L megacity` reports the two current Megacity shards, confirming the documented command is stale.

**Proposed boundary**

- Correct the canonical host architecture and test commands.
- Update the Megacity guide to build `draxul-test-megacity` and run `ctest -L megacity`.
- Add concise local guides for SatView and ScoreView covering target ownership, generated assets, worker lifetimes, optional-off requirements, focused test labels, and Vulkan/Metal obligations.
- Extend the test wrapper with a narrow stable entry point such as:

```text
python do.py test --label satview
python do.py test --label megacity
python do.py test --label scoreview-host
```

The wrapper should map each label to its focused build target before invoking CTest.

**Dependency shape**

Documentation and Python/CMake tooling only. `tests/CMakeLists.txt` remains the authoritative target/label inventory.

**Migration path**

1. Fix commands and removed symbols/files first.
2. Add tests for `do.py test --label`.
3. Add SatView and ScoreView local guides using the Megacity nested-guide pattern.
4. Add a lightweight documentation check that validates named CTest targets or source paths where practical.
5. Update prose counts from generated/current CMake inventory rather than hard-coding shard totals.

**Testing improvement**

Agents receive a dependable module-level build/test command. Python tests can verify label parsing, target selection, and command construction without executing tests.

**Agent-work benefit**

A bounded module task no longer defaults to the entire aggregate suite, and agents stop following commands or architecture that cannot exist in the current tree.

**Risks and prerequisites**

- Final target names should follow the host/Kanban target decisions above.
- Keep root guidance broad and detailed product rules local.
- Do not duplicate the already-tracked core/extras registration design or agent-script deduplication work.

---

## 7. Split Kanban’s pure core from its host integration

**Priority:** P2

**Location**

- [`modules/kanban/draxul-kanban/CMakeLists.txt:1-20`](/D:/dev/Draxul/modules/kanban/draxul-kanban/CMakeLists.txt:1)
- [`kanban_board.h:11-51`](/D:/dev/Draxul/modules/kanban/draxul-kanban/include/draxul/kanban/kanban_board.h:11)
- [`kanban_layout.h:13-55`](/D:/dev/Draxul/modules/kanban/draxul-kanban/include/draxul/kanban/kanban_layout.h:13)
- [`kanban_store.h:11-25`](/D:/dev/Draxul/modules/kanban/draxul-kanban/include/draxul/kanban/kanban_store.h:11)
- [`kanban_host.h:23-88`](/D:/dev/Draxul/modules/kanban/draxul-kanban/include/draxul/kanban/kanban_host.h:23)

**Current structural problem**

The single `draxul-kanban` target mixes:

- Board and selection value types.
- Filesystem storage and reordering.
- Pure layout.
- Navigation state.
- `GridHostBase` integration, rendering, key repeat, preview orchestration, and provider registration.

Only `kanban_host.cpp` needs host/grid integration. Navigation needs SDL key constants privately; board, store, and layout do not. Nevertheless every core consumer and unit test gets the complete `draxul-host` PUBLIC closure.

Markdown already demonstrates the cleaner pattern with separate core and host targets.

**Proposed boundary**

- Keep `draxul-kanban` for board, store, layout, and navigation.
- Add `draxul-kanban-host` for `KanbanHost` and provider registration.
- Link the executable to the host target.
- Make `draxul-kanban-host` expose the core transitively only where its public header genuinely requires it.

**Dependency shape**

- `draxul-kanban` → types; SDL private for navigation implementation.
- `draxul-kanban-host` → Kanban core and grid-host/host API.
- Pure Kanban tests → core only.
- Host lifecycle/rendering tests → host target.

**Migration path**

1. Add the host target while leaving source paths unchanged.
2. Move only `kanban_host.cpp` to it.
3. Update executable and test links.
4. Add a narrow provider header or test-internals surface when the separately tracked registration work settles.
5. Add a Kanban core public-header isolation build.

**Testing improvement**

Board parsing, storage, layout, and navigation tests can build without renderer, Neovim, PTY, or host lifecycle code. Host tests retain the existing grid-host fixture.

**Agent-work benefit**

Storage/layout changes and host/rendering changes become independent work packages. Kanban core becomes understandable without terminal-host context.

**Risks and prerequisites**

- Preserve SDL key behavior in navigation on both platforms.
- Keep filesystem behavior cross-platform.
- Coordinate the final host link with the proposed host-contract target names.

---

## Proposed target map

```text
draxul
├── draxul-app
├── draxul-markdown-host
│   └── draxul-markdown
├── draxul-kanban-host
│   └── draxul-kanban
├── draxul-satview-host
│   ├── draxul-satview-services
│   ├── draxul-satview-scene
│   │   └── satview scene composer
│   ├── draxul-satview-core
│   └── draxul-satview-renderer
├── draxul-megacity
│   ├── draxul-megacity-model
│   ├── draxul-code-semantics
│   ├── draxul-codeviz-scene
│   ├── draxul-codeviz-host
│   └── draxul-codeviz-renderer
└── draxul-scoreview-host
    ├── draxul-scoreview
    ├── draxul-score-learn
    ├── draxul-score-input
    ├── draxul-score-audio
    └── draxul-notation

draxul-host-api
├── draxul-grid-host
│   ├── draxul-terminal-host
│   │   ├── draxul-terminal-core
│   │   └── draxul-nvim-transport
│   │       └── draxul-nvim-protocol
│   └── grid-based product hosts
└── non-grid product hosts
```

All compiled internal targets should opt into one locally applied build-policy function.

## Best isolated work packages for parallel agents

1. **Build-policy prerequisite:** own the CMake helper and policy audit. Land before new target creation.
2. **Host architecture owner:** create host API/grid/terminal target seams. This owner should control `libs/draxul-host` and cross-module link migrations.
3. **Neovim protocol owner:** split `libs/draxul-nvim`; coordinate only the final terminal-host link with package 2.
4. **Kanban owner:** split core/host entirely within `modules/kanban` plus focused tests.
5. **SatView presentation owner:** extract scene composition and host panels without touching renderer backend files.
6. **Megacity model owner:** extract model/layout/routing without touching renderer or host lifecycle files.
7. **Guidance/tooling owner:** update documentation and label-level commands after target names stabilize.

The product packages can proceed concurrently after the build-policy and host target contracts are fixed. Avoid concurrent edits to root CMake or `app/main.cpp`; the latter is also part of the already-tracked core/extras registration seam.

## Strong existing qualities to preserve

- Optional product modules are gated and linked at the executable boundary.
- SatView, ScoreView, and Megacity already have meaningful lower-layer static libraries.
- Markdown already models a clean core/host split.
- Renderer backends keep Vulkan and Metal details out of neutral scene contracts.
- Shared Vulkan resource helpers retain explicit caller-owned synchronization and lifetime policy.
- Focused CTest executables and labels now permit parallel module testing.
- Test-only access is named through `*-test-internals` targets rather than added directly to production consumers.
- Public-header link-isolation targets catch missing dependency closure for foundational APIs.
- Immutable snapshots, generation checks, and deterministic layout rules are documented and widely used.
- Shader ABI parity has a mechanical contract and is already tracked, so it should be extended rather than replaced.

