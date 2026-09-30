# Refactor review consensus

**Model:** `GPT-6`  
**Scope:** The two refactor reviews named in `INPUT_REVIEWS/index.md`, checked against the current source tree. This was a read only review; no build, test, or project binary was run. The cards below are proposed work items for the trusted runner to create.

## Coverage and reconciliation

Claude reviewed the app, transport, server, rendering, build system, tests, and all five products in depth. Codex independently added narrower test and product boundaries, especially for NanoVG, MegaCity workers, SatView scene composition, Rezonality’s compiler, and Kanban timing. Their strongest overlap is the broad test executable, duplicated remote command queues, NanoVG CPU batches, and MegaCity worker ownership.

The current tree changes several conclusions. Root `kanban/pending/` contains `01 focus-and-name-new-tabs -bug.md`; all five product submodules are initialized and their pending lanes are empty. The root card does not cover these refactors. Current code already has GUI action parity checks, binary terminal-input codecs, an external SDK smoke, and a documented standalone fake remote terminal. Those facts narrow the proposed work below.

**Priority and sequence.** No planning refactor warrants P0. Start with test ownership and scope registration (card 02), then land independently owned client, protocol, server, process, and product changes. Land shared plugin/render contracts before product adapter migrations. Serialize changes that touch `app/app.cpp` and SatView’s runtime. Each implementation slice should keep the tree buildable, finish with the appropriate aggregate test and same-cache smoke, and leave unavailable Windows/macOS or Vulkan/Metal gates unchecked.

The card’s `Source` names its owning source file. A proposed focused target or command becomes available only after that card implements it.

## Accepted boundaries

| Cards | Agreed target and dependency direction | Credit and ownership |
|---|---|---|
| 02, 16, 20 | Explicit test ownership, CMake-owned scope inventory, private test seams and shared plugin fixtures; production target graph remains authoritative | Claude 1, 2, 18, 30, 32, 36, 44; Codex 1, 13. One CMake owner coordinates independent suite moves. |
| 03, 04, 13, 14 | Headless CLI and launch policy below app; typed, app-private orchestration collaborators; narrow pane-launch/config values | Claude 3–5, 14, 27–29. One app owner per card; serialize shared-file edits. |
| 05–09, 12, 15 | Shared client queue and request policy; typed protocol vocabulary; private agent/checkpoint policy; neutral PTY process factory and PID identity | Claude 6–11, 31; Codex 2, 9, 10. Separate client, protocol, server and process owners. |
| 10–11 | Session values and validation beneath a new store; staged atomic replacement beneath persistence callers | Claude 12–13. Store extraction precedes broad replacement migration. |
| 17–19 | Shared shader tooling, product-owned render manifests, explicit installed C++ SDK exports | Claude 37, 39, 42. Core tooling lands before product adoption. |
| 21–28, 30–31 | Neutral render contracts; shared plugin ABI translation; native operation seams; NanoVG CPU batches and frame driver; Markdown GPU plan; ImGui and atlas seams; CPU render harness | Claude 15–17, 19–22, 26, 38, 41; Codex 4, 11, 12. Keep Vulkan/Metal encoding in native backends. |
| 29 | Value-oriented Kanban interaction policy within existing core target | Claude 40; Codex 8. One built-in module owner. |
| Product cards below | Private workers, frame preparation, test isolation, configuration/scene ownership, compiler/test boundaries, header ownership and PCB selection | Claude 24, 25, 43, 45–49; Codex 3, 5–7. Each product owns its card and commits. |

The descriptions and checklists in each card specify the public interface, private implementation, callers, migration, narrow verification, parity, dependencies, and suggested owner.

## Rejected claims, tracked work, and unresolved leads

- **Preserve the fake remote terminal.** `docs/features.md` documents `--experimental-remote-terminal`; `app/main.cpp` still creates its coordinator-free host. Claude 6’s wholesale transport removal is rejected. Card 05 shares only value-level queue policy and keeps both workers.
- **Keep domain-specific server semantics.** Claude 8’s universal route table and public-`Impl` diagnosis would force a broad rewrite; card 08 isolates agent policy. Claude 34’s single idempotency cache would merge different key, replay, and conflict rules across agents, topology, terminals, and streams.
- **Preserve existing contracts.** GUI action parity already exists; the menu invokes `GuiActionHandler::execute`. The terminal protocol already has binary input codecs. An external SDK smoke already exists. Cards extend these contracts rather than recreate them. The proposed old test regex “for one release” is omitted because repository guidance rejects compatibility layers.
- **Do not infer behavioral parity from duplicated code.** Markdown sampler, upload-rectangle and revision differences require characterization; card 25 preserves current backend policies. An MSVC duplicate-symbol failure from the plugin ImGui fork is unverified; card 26 addresses the confirmed API ownership problem and includes a Windows link check.
- **Do not expand narrow changes into rewrites.** An App pimpl, symmetric renderer-file splitting, a complete coordinator connection/sink redesign, universal product persistence schemas, ScoreView layout/gate controllers, and broad PCBView drawing extraction lack a demonstrated additional boundary. A simple `IRpcChannel` factory cannot cover `NvimHost` spawn, callback, drain and shutdown behavior; a lifecycle-complete interface remains an unresolved lead.
- **Keep intentional ABI use.** PCBView’s raw native frame records match `IPluginNanoVGPass`; converting them wholesale is rejected. Its route picking has a separate accepted ownership change.
- **Already tracked:** SatView ice-box cards `40 satview-atmosphere-scattering-deduplication -refactor.md`, `41 satview-network-privacy-controls -feature.md`, and `42 satview-shader-abi-parity -test.md`; ScoreView ice-box cards 18 and 19; and completed ScoreView flow-analysis card 05. Completed SatView worker/pause and root storage/input regression cards remain regression evidence, not new refactor tasks.
- **Remaining verification gaps:** static review cannot establish native link/export behavior, timing, render parity, or the eventual build closure of new targets. PCBView’s private CI availability and any general product drawing split remain unresolved; no task is created for either.

## Target module map

```text
Foundations:
  draxul-types (+ PID identity)
  draxul-session-model (values/validation)
  draxul-atomic-file (new, staged replacement)

Persistence and wire:
  draxul-session-store (new) -> session-model, atomic-file
  draxul-protocol -> session-model
  draxul-control -> draxul-client (injected request/recovery)
  draxul-client (shared terminal queue policy)
  draxul-terminal-process (neutral factory -> private ConPTY/Unix)
  draxul-server -> protocol, session-store, terminal-process
      private agent policy and checkpointer

Rendering and plugin support:
  draxul-render-contract -> typed Vulkan/Metal context leaves
  draxul-renderer -> render-contract (native backends remain here)
  draxul-nanovg-backend (+ CPU batch and frame helpers)
  draxul-imgui-core (+ scalar input injection; no GPU dependency)
  plugin ImGui backend -> private Vulkan fork API
  draxul-render-harness-core -> render-test driver/demo host
  plugin frame/input helpers -> SDK and allowlisted support leaves

Hosts, built-ins and app:
  host-api -> grid-host -> terminal-host / nvim-host
  plugin-host and markdown-host -> render contract + native context
  draxul-markdown -> markdown-host
  draxul-kanban (+ interaction policy) -> kanban-host
  draxul-app-cli -> headless client/control/protocol leaves
  draxul-app -> app-private collaborators and production hosts
  draxul executable -> app, CLI, platform entry points

Products:
  MegaCity model/scene -> private host workers and renderer frame prep
  SatView core -> scene composition; core + scene + services -> runtime
  ScoreView pipeline + audio -> runtime -> module
  PCBView core (routing + selection) -> runtime -> module
  Rezonality project/runtime/audio -> private module objects -> module
```

Static libraries should be added only where they remove a real dependency edge: CLI, session store, atomic replacement, and render harness core. Most other accepted work belongs inside existing targets. Product test executables may split when their link closure is demonstrably narrower.

## Repository guidance and validation entry points

Update canonical `CLAUDE.md` to distinguish built-in `modules/` from product-owned `plugins/<product>/`, include `--rezonality`, and replace the presently broad `draxul-test-core` narrow-loop example after card 02. Keep root `AGENTS.md` as a pointer. Correct `GEMINI.md`’s reference to absent `docs/features/`. Move MegaCity’s guide to its product root and include its model test target; add concise ScoreView and PCBView guides. Existing SatView and Rezonality guides remain the product authorities.

CMake should own test scope and aggregate membership. `do.py` must preserve the distinction between unit, integration, and selected render tests, as well as `--label` intersection. Add configure/link checks for new headless and contract boundaries. Do not claim a narrow command is available until its target is registered.

# Proposed work items

### kanban/pending/00 test-graph-and-scope -refactor.md

# Make test targets and scope selection authoritative

**Priority:** P1 — broad test links slow and couple otherwise independent work.  
**Source:** `tests/CMakeLists.txt`  
**Proposed by:** Claude 1, 2, 18, 36; Codex 1, 13. **Owner:** one core test/CMake agent, with coordinated suite moves.  
**Evidence:** `draxul-test-core` links renderer, window, font, server and hosts and depends on `draxul`; `do.py` omits built app-shell/host-api and MegaCity model tests. Markdown parser cases and plugin tests sit in host/app-linked targets.

**Boundary verification**
- [ ] Inventory every test case, tag, platform gate, executable dependency, CTest label and product aggregate.
**Implementation and migration**
- [ ] Split protocol, server/client, process, font and renderer suites by owning library; move only executable-dependent cases to integration. Separate Markdown core/host and plugin-loader/storage suites. Keep source-partition tripwires.
- [ ] Let CMake register unit scope and product aggregates; make `do.py` consume that inventory while preserving integration/render selection and `--label` intersection. Move keycode-only Nvim/Kanban links to `SDL3::Headers`.
**Unit tests**
- [ ] Check case/label parity, zero-match handling, disabled products, and narrow target closure. Make prospective `do.py test debug --target draxul-test-server` and Markdown-core targets usable.
**Cross-platform validation**
- [ ] Exercise ConPTY/Unix gated suites, Windows/macOS configure, core aggregate and same-cache smoke; keep render cases platform-filtered.
**Agent documentation and tooling**
- [ ] Update `CLAUDE.md`, `do.py` tests and CI scope expectations.
**Acceptance criteria**
- [ ] Every existing case runs in its intended scope; a focused headless target does not build the app, GPU stack or staged products.

### kanban/pending/03 headless-cli-launch -refactor.md

# Extract headless CLI and launch policy

**Priority:** P1 — CLI parsing and launch decisions currently inherit the graphics app.  
**Source:** `app/CMakeLists.txt`  
**Proposed by:** Claude 4, 5, 28. **Owner:** one app/CLI agent. **Depends on:** card 02 for a narrow test target.  
**Evidence:** CLI files compile into `draxul-app`; `main.cpp` mixes server/bootstrap decisions with native entry points; `ParsedArgs` layout depends on a render-test define.

**Boundary verification**
- [ ] Record CLI grammar, environment precedence, helper paths and Windows/macOS launch arguments.
**Implementation and migration**
- [ ] Add headless `draxul-app-cli` for parsers, dispatch, session resolution and injected IO/request ports. Make `ParsedArgs` fields unconditional.
- [ ] Move pure bootstrap/status/argument decisions incrementally; keep console, detached helper execution, status surface and `AppOptions` mapping in app/platform code. Put executable layout in the client.
**Unit tests**
- [ ] Add `draxul-test-cli` grammar, malformed-input, capability, output, path and quoted/unicode-argument cases.
**Cross-platform validation**
- [ ] Verify Windows helper and macOS bundle launch, aggregate tests and same-cache startup smoke.
**Agent documentation and tooling**
- [ ] Add CLI target and headless closure to module map and dependency checks.
**Acceptance criteria**
- [ ] CLI tests build without renderer/window; existing CLI behavior and native launch remain intact.

### kanban/pending/04 app-orchestration-collaborators -refactor.md

# Give App’s projection, mutation and command effects explicit owners

**Priority:** P1 — `app/app.cpp` is a shared collision point with coupled state and callbacks.  
**Source:** `app/app.cpp`  
**Proposed by:** Claude 3, 14. **Owner:** one app agent; separate command work only after ports land. **Depends on:** card 03 for clean launch ownership.  
**Evidence:** remote reconcile, local mutation, agent launch, checkpoint effects and control effects share the App translation unit; `GuiActionHandler::Deps` is large, while action parity already exists.

**Boundary verification**
- [ ] Characterize epoch/preview/focus, local mutation, host creation, input route and command call order.
**Implementation and migration**
- [ ] Add safe tab/pane lookup operations. Extract one app-private reconciler/effect collaborator at a time with typed host/layout/frame ports; keep `TopologyProjection` in client.
- [ ] Group GUI command bindings by responsibility while retaining `execute()` and its canonical registry.
**Unit tests**
- [ ] Exercise projection retries, epoch reset, failed application, mutation ordering and command effects through fake ports; retain App smoke.
**Cross-platform validation**
- [ ] Check macOS menu dispatch and Windows actions, core aggregate and same-cache smoke.
**Agent documentation and tooling**
- [ ] Document app-private ownership and update affected target sources.
**Acceptance criteria**
- [ ] App keeps orchestration order; extracted policy is testable without a live window or renderer.

### kanban/pending/05 remote-command-queue -refactor.md

# Share remote terminal command queue policy

**Priority:** P1 — duplicated bounded-input rules can diverge across supported paths.  
**Source:** `libs/draxul-client/src/remote_session_coordinator.cpp`  
**Proposed by:** Codex 2; Claude 6, 31 narrowed. **Owner:** one client/terminal-host agent.  
**Evidence:** coordinator and `remote_terminal_host.cpp` repeat 128-command/48 KiB bounds, request IDs, resize replacement and scroll coalescing.

**Boundary verification**
- [ ] Pin framed input, retry-ID retention, mergeability, bounds and standalone fake-terminal behavior.
**Implementation and migration**
- [ ] Add a value-only queue in `draxul-client` with injected ID source; migrate host, then coordinator. Keep their locks, wakes, workers and transports.
**Unit tests**
- [ ] Test atomic multi-chunk input, saturation, coalescing, retry and IDs via client test-internals.
**Cross-platform validation**
- [ ] Retain coordinator and fake-terminal integration on Windows/macOS; run aggregate and same-cache smoke.
**Agent documentation and tooling**
- [ ] Document why the coordinator-free diagnostic path remains supported.
**Acceptance criteria**
- [ ] One queue policy serves both paths without changing their lifecycle or transport behavior.

### kanban/pending/06 client-control-recovery -refactor.md

# Inject client control requests and recovery policy

**Priority:** P1 — direct static requests force socket tests and duplicate handshake retry.  
**Source:** `libs/draxul-client/src/server_control_channel.cpp`  
**Proposed by:** Claude 11. **Owner:** one client agent. **Related:** card 05.  
**Evidence:** `ServerControlChannel` calls static `ControlClient::request`; remote terminal client repeats token/handshake refresh logic.

**Boundary verification**
- [ ] Record retry budgets, token refresh, endpoint naming and concurrent identity behavior.
**Implementation and migration**
- [ ] Inject request and refresh/probe operations into the control channel; compose compatible callers incrementally. Keep native endpoint naming in the production adapter.
**Unit tests**
- [ ] Script handshake failure, refresh, newer identity, retry and final error mapping without sockets; retain IPC coverage.
**Cross-platform validation**
- [ ] Test Windows namespaced endpoints and POSIX sockets, aggregate and same-cache smoke.
**Agent documentation and tooling**
- [ ] Expose only the test seam required by callers.
**Acceptance criteria**
- [ ] Client recovery has one policy and production timing/error behavior is preserved.

### kanban/pending/07 protocol-vocabulary-and-requests -refactor.md

# Type shared server vocabulary and terminal requests

**Priority:** P2 — repeated literals and request assembly increase wire drift risk.  
**Source:** `libs/draxul-protocol/include/draxul/remote_terminal_protocol.h`  
**Proposed by:** Claude 7, narrowed. **Owner:** one protocol agent. **Depends on:** card 02 for narrow tests.  
**Evidence:** client/server capability lists and terminal request construction are repeated; binary input codec already exists.

**Boundary verification**
- [ ] Separate supported, requested and required capability meanings and inventory exact wire names.
**Implementation and migration**
- [ ] Add named capability/method values and typed request codecs in existing protocol target; migrate callers by family and reuse binary-input codec.
**Unit tests**
- [ ] Round-trip valid/invalid requests, binary input and negotiation; test server support covers required client capabilities.
**Cross-platform validation**
- [ ] Run protocol aggregate on Windows/macOS and same-cache smoke.
**Agent documentation and tooling**
- [ ] Document current-format wire ownership without compatibility aliases.
**Acceptance criteria**
- [ ] No duplicate request construction remains for migrated methods; wire format is unchanged.

### kanban/pending/08 server-agent-command-policy -refactor.md

# Isolate server agent command policy

**Priority:** P1 — validation and replay policy is entangled with kernel routing.  
**Source:** `libs/draxul-server/src/server_kernel_requests.cpp`  
**Proposed by:** Codex 9; Claude 8 narrowed. **Owner:** one server agent.  
**Evidence:** agent route handles request-ID validation, replay and launch/restart/input effects in the central dispatcher.

**Boundary verification**
- [ ] Inventory authentication order, error precedence, generation checks and successful-only replay keys.
**Implementation and migration**
- [ ] Add a private agent-command collaborator with typed effect/snapshot operations; migrate one operation family at a time. Keep Session lookup, state thread and terminal ownership in kernel.
**Unit tests**
- [ ] Fake operations for duplicate success, failed noncache, invalid IDs, generation and launch/restart mapping; retain process integration.
**Cross-platform validation**
- [ ] Run server aggregate and same-cache smoke on both platforms.
**Agent documentation and tooling**
- [ ] State the private policy/kernel split in server guidance.
**Acceptance criteria**
- [ ] Agent policy is isolated without a universal router rewrite or changed replay semantics.

### kanban/pending/09 server-checkpoint-state -refactor.md

# Own server checkpoint transitions in one collaborator

**Priority:** P1 — write state and shutdown flush span multiple kernel files.  
**Source:** `libs/draxul-server/src/server_kernel_sessions.cpp`  
**Proposed by:** Claude 9. **Owner:** one server agent. **Related:** card 10.  
**Evidence:** checkpoint state is in `server_kernel_impl.h`, writer in sessions, and scheduling/final flush in lifecycle.

**Boundary verification**
- [ ] Characterize revision, in-flight, failed save, last-good and shutdown-deadline behavior.
**Implementation and migration**
- [ ] Add private `SessionCheckpointer` with injected save, clock and executor; migrate scheduling and result collection without changing durable save behavior.
**Unit tests**
- [ ] Use fake time/executor for N-to-N+1 flush, failure and deadline cases; retain IPC persistence tests.
**Cross-platform validation**
- [ ] Preserve Windows replacement and POSIX durability; run aggregate and same-cache smoke.
**Agent documentation and tooling**
- [ ] Document state ownership and shutdown ordering.
**Acceptance criteria**
- [ ] Status strings and last-good checkpoint semantics remain stable.

### kanban/pending/10 session-model-store -refactor.md

# Separate Session values from codec and storage

**Priority:** P1 — protocol inherits TOML and durable-file implementation.  
**Source:** `libs/draxul-session-model/src/session_state.cpp`  
**Proposed by:** Claude 12. **Owner:** one session/core agent. **Precedes:** card 11.  
**Evidence:** one source contains validation, TOML codec and platform file replacement; protocol links session-model publicly.

**Boundary verification**
- [ ] Trace model-only callers, codec callers and durable persistence error paths.
**Implementation and migration**
- [ ] Keep values/pure validation in `draxul-session-model`; add `draxul-session-store` for codec/path/list/IO with `store → model`. Retarget app/server/CLI explicitly.
**Unit tests**
- [ ] Isolated validation and codec round trips; retain corruption and transactional persistence cases.
**Cross-platform validation**
- [ ] Check Windows replacement and macOS/POSIX flush, headless closure, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Update module map and dependency-boundary checks.
**Acceptance criteria**
- [ ] Protocol/client model consumers no longer inherit document/file dependencies.

### kanban/pending/11 atomic-file-replacement -refactor.md

# Share staged atomic file replacement

**Priority:** P2 — several callers repeat publication stages with different guarantees.  
**Source:** `libs/draxul-host/src/plugin_storage.cpp`  
**Proposed by:** Claude 13, narrowed. **Owner:** one storage/core agent. **Depends on:** card 10 for store migration.  
**Evidence:** session, plugin storage, agent integration, config and control transport each implement temp/write/replace; plugin storage already has fault injection.

**Boundary verification**
- [ ] Record each caller’s permissions, durability, exclusive temp and post-replace error contract.
**Implementation and migration**
- [ ] Add `draxul-atomic-file` with explicit stage/options and injected operations; migrate plugin storage, Session store, agent/config, then security-sensitive control transport.
**Unit tests**
- [ ] Fault-inject before and after replacement, cleanup, directory flush and mode/ACL handling.
**Cross-platform validation**
- [ ] Verify `MoveFileExW`/ACL and POSIX mode/fsync behavior, aggregate and smoke on each OS.
**Agent documentation and tooling**
- [ ] Record caller-specific options and error mapping.
**Acceptance criteria**
- [ ] Each migrated caller retains its current security and durability guarantees.

### kanban/pending/12 terminal-process-factory -refactor.md

# Hide native PTY types behind a process factory

**Priority:** P1 — server runtime publicly selects ConPTY/Unix implementations.  
**Source:** `libs/draxul-terminal-process/include/draxul/conpty_process.h`  
**Proposed by:** Claude 10, narrowed. **Owner:** one terminal-process/server-runtime agent.  
**Evidence:** public ConPTY header exposes `windows.h` and `HANDLE`; Unix spawn differs; server runtime switches native process and spawn logic.

**Boundary verification**
- [ ] Pin shell candidate order, environment, login behavior, callback and writer-retirement order.
**Implementation and migration**
- [ ] Add neutral spawn options/process factory and pure shell resolver; inject factory into `ServerTerminalRuntime`. Keep concrete native types private and runtime state/API intact.
**Unit tests**
- [ ] Fake process and cross-platform shell-resolution cases; retain ConPTY, PTY and fd-inheritance integration.
**Cross-platform validation**
- [ ] Verify Windows cancel-before-handle-destruction and POSIX close-before-join, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Document borrowed writer-handle and shutdown ownership.
**Acceptance criteria**
- [ ] Public process API contains no native handle types; no terminal behavior changes.

### kanban/pending/13 pane-launch-values -refactor.md

# Narrow pane launch dependencies

**Priority:** P2 — PaneManager and render tests inherit the whole graphics-heavy `AppOptions`.  
**Source:** `app/pane_manager.h`  
**Proposed by:** Claude 27. **Owner:** one app/pane agent. **Coordinates with:** cards 04 and 30.  
**Evidence:** PaneManager receives `AppOptions` and resolves three host-factory paths; render-test header also returns `AppOptions`.

**Boundary verification**
- [ ] Record launch defaults, environment and test-override precedence.
**Implementation and migration**
- [ ] Add app-owned `PaneLaunchDefaults`, resolved factory and registry reference; move neutral render scenario values out of `AppOptions` callers.
**Unit tests**
- [ ] Verify overrides, unavailable hosts and projected pane creation.
**Cross-platform validation**
- [ ] Check default shell/environment on Windows/macOS, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Clarify AppOptions ownership in module map.
**Acceptance criteria**
- [ ] PaneManager’s public contract no longer requires the whole app option bundle.

### kanban/pending/14 effective-config-reload -refactor.md

# Isolate checked config loading and reload differences

**Priority:** P2 — startup/reload duplicate loading while change classification lives inside App.  
**Source:** `app/app.cpp`  
**Proposed by:** Claude 29, narrowed. **Owner:** one app/config agent; serialize with card 04.  
**Evidence:** App repeats document/config loading and classifies changes inline; startup fallback and transactional reload intentionally differ.

**Boundary verification**
- [ ] Characterize fallback, override, staged font and all-or-old reload rules.
**Implementation and migration**
- [ ] Extract checked effective-config bundle and pure delta classifier; retain App publication and side effects.
**Unit tests**
- [ ] Test bundle errors and delta values; retain App reload failure/atlas lifetime integration.
**Cross-platform validation**
- [ ] Test font/config paths on Windows/macOS, aggregate and same-cache smoke.
**Agent documentation and tooling**
- [ ] Document the loader versus App transaction responsibility.
**Acceptance criteria**
- [ ] Failed reload leaves previous config and font state intact.

### kanban/pending/15 pid-process-identity -refactor.md

# Centralize PID start identity

**Priority:** P2 — client and server duplicate OS token formatting.  
**Source:** `libs/draxul-types/src/process_util.cpp`  
**Proposed by:** Codex 10. **Owner:** one process/core agent.  
**Evidence:** `server_client.cpp` and `server_kernel_lifecycle.cpp` separately derive macOS/Linux start tokens; process utility already has a Windows handle form.

**Boundary verification**
- [ ] Compare exact token bytes and PID-reuse protections.
**Implementation and migration**
- [ ] Add PID lookup to existing process utility and migrate producer/consumer; retain client liveness and held-handle checks.
**Unit tests**
- [ ] Current/missing PID and token-format cases plus lifecycle/discovery integration.
**Cross-platform validation**
- [ ] Verify Windows/macOS behavior, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Document token ownership in process utility.
**Acceptance criteria**
- [ ] One implementation formats process identity without changing tokens.

### kanban/pending/16 internal-include-test-seams -refactor.md

# Enforce private include and test boundaries

**Priority:** P2 — relative private-source includes and broad exported usage requirements hide coupling.  
**Source:** `cmake/CheckDependencyBoundaries.cmake`  
**Proposed by:** Claude 30, 32 narrowed. **Owner:** one core CMake/ownership agent. **Follows:** cards 02–04 where affected.  
**Evidence:** host leaves share include roots; tests reach client, NanoVG, Kanban and server `src/` by relative paths; app exports its source directory and many links.

**Boundary verification**
- [ ] Inventory each private include and public-header dependency before changing link visibility.
**Implementation and migration**
- [ ] Add narrow `*-test-internals` targets and missing link-isolation consumers; migrate private includes; demote only usage requirements proven private.
**Unit tests**
- [ ] Add configure checks against new relative private-source includes and link/compile isolation.
**Cross-platform validation**
- [ ] Configure Windows/macOS and run core aggregate and smoke.
**Agent documentation and tooling**
- [ ] Document public-header versus test-internals rule.
**Acceptance criteria**
- [ ] Tests have explicit private access; production consumers compile from declared interfaces.

### kanban/pending/17 shader-build-helper -refactor.md

# Centralize shader compilation and payload declarations

**Priority:** P2 — repeated GLSL/Metal command loops drift across products.  
**Source:** `cmake/CompileShaders_Metal.cmake`  
**Proposed by:** Claude 37. **Owner:** one CMake agent, then product owners.  
**Evidence:** core, spinning triangle and product CMake files repeat glslc/xcrun output and dependency construction.

**Boundary verification**
- [ ] Inventory flags, output names, include dependencies, staging and standalone product requirements.
**Implementation and migration**
- [ ] Add explicit CMake shader/payload helper; migrate core/reference plugin first, then products without renaming current targets.
**Unit tests**
- [ ] Configure fixtures and include-change rebuild checks.
**Cross-platform validation**
- [ ] Verify GLSL/Vulkan on Windows and MSL/Metal on macOS; run affected aggregate and smoke.
**Agent documentation and tooling**
- [ ] Ship or document helper for standalone product builds.
**Acceptance criteria**
- [ ] Shader changes rebuild the right output and stage the same payload on both platforms.

### kanban/pending/18 product-render-manifests -refactor.md

# Let products own render scenarios and references

**Priority:** P2 — every product currently edits the root render manifest.  
**Source:** `tests/render/manifest.json`  
**Proposed by:** Claude 39. **Owner:** one core registration agent, then product owners. **Depends on:** card 02 scope metadata.  
**Evidence:** root manifest holds product scenarios; CMake and `do.py` assume root paths. MegaCity/ScoreView CTests lack product target gating.

**Boundary verification**
- [ ] Inventory scenario names, paths, platforms, required targets, scopes and bless commands.
**Implementation and migration**
- [ ] Add product manifest/root registration; update CMake and `do.py` scenario resolution; migrate products in separate commits.
**Unit tests**
- [ ] Test global name uniqueness, disabled/uninitialized products, paths, CTest inventory and command mapping.
**Cross-platform validation**
- [ ] Preserve Windows/macOS goldens and platform filters; run affected render cases, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Update product render guidance and bless entry points.
**Acceptance criteria**
- [ ] A disabled product registers no product render test; core no longer owns its scenario assets.

### kanban/pending/19 installed-cxx-sdk-targets -refactor.md

# Export explicit installed C++ plugin support targets

**Priority:** P2 — standalone consumers reconstruct installed link edges.  
**Source:** `sdk/CMakeLists.txt`  
**Proposed by:** Claude 42; product evidence from Claude 48. **Owner:** one SDK agent, then ScoreView owner.  
**Evidence:** SDK exports `PluginSDK` only while types/performance/helper headers are installed elsewhere; ScoreView manually imports them and hand-rolls path services.

**Boundary verification**
- [ ] Inventory shipped C++ headers, transitive GLM/JSON dependencies and current ScoreView extraction requirements.
**Implementation and migration**
- [ ] Keep pure C ABI target; export optional current-version C++ support targets and bounded service helpers, then migrate standalone ScoreView.
**Unit tests**
- [ ] Extend existing external SDK smoke to compile/link every shipped C++ surface used by products.
**Cross-platform validation**
- [ ] Test Windows and macOS installed package/extraction plus aggregate and smoke.
**Agent documentation and tooling**
- [ ] Document C versus C++ SDK ownership and installed targets.
**Acceptance criteria**
- [ ] Standalone product no longer reconstructs private archive paths.

### kanban/pending/20 plugin-runtime-test-fixtures -refactor.md

# Give product runtime tests contract-level fixtures

**Priority:** P2 — product unit tests inherit core host/renderer fakes.  
**Source:** `tests/support/fake_renderer.h`  
**Proposed by:** Claude 44. **Owner:** one shared test-support agent, then product test owners.  
**Evidence:** MegaCity/SatView fixtures include core fake window/renderer and link host/renderer despite runtime-level subjects; plugin dependency audit checks production targets only.

**Boundary verification**
- [ ] Separate legitimate host-loading integration cases from runtime-contract unit cases.
**Implementation and migration**
- [ ] Add contract-only frame/callback fakes using existing runtime callbacks; migrate suitable product tests and add a narrow test-target allowlist.
**Unit tests**
- [ ] Compile runtime tests from declared plugin interfaces; preserve host integration fixtures.
**Cross-platform validation**
- [ ] Check Vulkan/Metal header closure, product aggregates and same-cache smoke.
**Agent documentation and tooling**
- [ ] Document fixture selection and allowed test dependencies.
**Acceptance criteria**
- [ ] Runtime unit targets avoid core renderer/host links unless their cases exercise those integrations.

### kanban/pending/21 render-contract-host-links -refactor.md

# Separate neutral render contracts from native contexts and retarget hosts

**Priority:** P1 — neutral consumers inherit native GPU libraries and concrete renderer links.  
**Source:** `libs/draxul-plugin-support/CMakeLists.txt`  
**Proposed by:** Claude 15, 16. **Owner:** one render-contract/CMake agent.  
**Evidence:** `draxul-plugin-render-support` transitively links Vulkan/VMA or Metal; all leaves expose one include root. Markdown-host publicly and plugin-host privately link `draxul-renderer` while using base/native context APIs.

**Boundary verification**
- [ ] Inventory base versus native context headers and host include/link needs.
**Implementation and migration**
- [ ] Introduce neutral contract and typed native context leaves with scoped include roots; retain the support alias as the aggregate. Retarget Markdown/plugin hosts to contract, native context, SDL headers and ImGui core as used.
**Unit tests**
- [ ] Link-isolation and configure rejection for neutral consumers reaching `draxul-renderer` or native GPU libraries.
**Cross-platform validation**
- [ ] Preserve Vulkan/VMA and Metal ARC/framework links; run host aggregate, relevant renders and smoke.
**Agent documentation and tooling**
- [ ] Update module map and allowlist checks.
**Acceptance criteria**
- [ ] Neutral consumers see only neutral headers; native passes still encode on both GPUs.

### kanban/pending/22 plugin-frame-input-bridge -refactor.md

# Share plugin frame mapping and primitive input translation

**Priority:** P1 — positional native contexts and copied adapter mapping drift by product.  
**Source:** `libs/draxul-host/src/plugin_render_pass_vk.cpp`  
**Proposed by:** Claude 17, with PCBView migration narrowed. **Owner:** one SDK/host seam agent, then product owners. **Depends on:** card 21.  
**Evidence:** host Vulkan/Metal frame packing hardcodes scale/PPI despite `PluginHost::plugin_viewport()`; MegaCity/SatView reconstruct contexts; product adapters differ on focus/composition/unknown input.

**Boundary verification**
- [ ] Pin frame fields, prepass semantics, input coordinate/capture order and supported event kinds per product.
**Implementation and migration**
- [ ] Add typed frame/context mapping and `VkRenderContext` descriptor in allowlisted support; pass actual viewport scale/PPI. Add primitive input conversion, then migrate products without changing their semantic handling.
**Unit tests**
- [ ] GPU-free context/frame round trips and complete event mapping, including unknown kinds and local/global coordinates.
**Cross-platform validation**
- [ ] Preserve Vulkan no-active-pass and Metal nil-encoder prepass; run affected product aggregates/goldens and smoke.
**Agent documentation and tooling**
- [ ] Document SDK helper ownership and product migration order.
**Acceptance criteria**
- [ ] Migrated adapters share field mapping while retaining product-specific event effects.

### kanban/pending/23 native-renderer-operation-tests -refactor.md

# Test native renderer operations through behavior seams

**Priority:** P1 — source-spelling assertions obstruct safe renderer changes.  
**Source:** `tests/nanovg_vulkan_sync_tests.cpp`  
**Proposed by:** Claude 19, 22 narrowed. **Owner:** one renderer agent, with platform review.  
**Evidence:** tests search renderer/NanoVG source for upload and frame statements; Vulkan already has `VkRendererOperations`; Metal capture has no equivalent seam, and both backends duplicate pitched channel conversion.

**Boundary verification**
- [ ] Map every statement check to the invariant it intends to protect; retain genuine CMake/shader-text checks.
**Implementation and migration**
- [ ] Extract capture/readback policy and pitched CPU conversion; add narrow native-operation recording where needed; replace only checks with equivalent behavior coverage.
**Unit tests**
- [ ] Fault, ordering, BGRA/pitch and NanoVG upload/retirement cases, including Metal completion-before-signal.
**Cross-platform validation**
- [ ] Run Windows Vulkan and macOS Metal renderer tests, shared snapshots, product scope where affected, and smoke.
**Agent documentation and tooling**
- [ ] Document native operation-seam expectations.
**Acceptance criteria**
- [ ] Tests survive implementation renames while protecting the same resource and synchronization behavior.

### kanban/pending/24 nanovg-cpu-batches -refactor.md

# Share NanoVG CPU batch construction

**Priority:** P1 — duplicated fill/stroke/triangle geometry risks backend drift.  
**Source:** `libs/draxul-nanovg/backend/src/nanovg_vk.cpp`  
**Proposed by:** Claude 20; Codex 4. **Owner:** one NanoVG agent across both backends.  
**Evidence:** Vulkan and Metal backends repeat fan expansion, path offsets, cover geometry and uniform decisions; paint conversion is already shared.

**Boundary verification**
- [ ] Record call order, uniform layout, stencil and Metal pre-sizing invariants.
**Implementation and migration**
- [ ] Add backend-private neutral batch records; move triangles, stroke, then fill. Leave blend, texture, resources and encoding native.
**Unit tests**
- [ ] Device-free geometry, offsets, uniforms and ordering in a narrow NanoVG target.
**Cross-platform validation**
- [ ] Check Vulkan and Metal render snapshots, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Document CPU batch versus native encoder ownership.
**Acceptance criteria**
- [ ] Both backends consume one CPU batch builder with unchanged visual output.

### kanban/pending/25 markdown-gpu-planning -refactor.md

# Extract common Markdown upload and batch planning

**Priority:** P1 — duplicated planning currently has unexplained backend differences.  
**Source:** `modules/markdown/draxul-markdown/src/markdown_render_pass_vk.cpp`  
**Proposed by:** Claude 21. **Owner:** one Markdown rendering agent. **Related:** card 21.  
**Evidence:** Vulkan clamps some upload rectangles and maximizes revision; Metal rejects and assigns; both duplicate range validation and push constants.

**Boundary verification**
- [ ] Characterize both existing rectangle, sampler and revision policies before moving code.
**Implementation and migration**
- [ ] Add CPU plan records for shared upload layout, atlas generation and validated batches; keep policy differences explicit and native resource/encoding work in each pass.
**Unit tests**
- [ ] Plain-data short-buffer, rectangle, revision and instance-range cases in Markdown layout/plan target.
**Cross-platform validation**
- [ ] Preserve both Vulkan and Metal output; run Markdown renders, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Add shader ABI assertions and record unresolved policy differences.
**Acceptance criteria**
- [ ] Common mechanics have one owner without silently changing backend behavior.

### kanban/pending/26 plugin-imgui-vulkan-api -refactor.md

# Give the plugin ImGui Vulkan fork an owned API

**Priority:** P2 — products include an upstream header while linking a private fork.  
**Source:** `plugins/support/imgui/CMakeLists.txt`  
**Proposed by:** Claude 26, revised. **Owner:** one plugin support agent, then MegaCity/SatView owners.  
**Evidence:** plugin support compiles a modified Vulkan backend; renderer compiles upstream; product renderers call texture functions through `<backends/imgui_impl_vulkan.h>`.

**Boundary verification**
- [ ] Inventory fork extensions, texture callers, symbol ownership and Metal class-prefix behavior.
**Implementation and migration**
- [ ] Add plugin-owned texture registration/removal wrapper and private backend target/API; migrate product callers and then reject upstream backend includes in products. Keep GPU code out of `draxul-imgui-core`.
**Unit tests**
- [ ] Wrapper lifecycle and compile checks for both host and plugin backend presence.
**Cross-platform validation**
- [ ] Inspect Windows link map/build with both implementations; preserve per-plugin Metal Objective-C class renaming; run product renders and smoke.
**Agent documentation and tooling**
- [ ] Document backend API ownership.
**Acceptance criteria**
- [ ] Product code uses the plugin-owned API; no unverified ODR failure is assumed.

### kanban/pending/27 imgui-input-injection -refactor.md

# Share primitive ImGui input injection

**Priority:** P2 — diagnostics and plugin wrappers repeat event injection.  
**Source:** `libs/draxul-imgui-core/src/sdl_imgui_input.cpp`  
**Proposed by:** Codex 11. **Owner:** one ImGui input agent.  
**Evidence:** `ui_panel.cpp` and `imgui_input_bridge.h` repeat modifiers, text, buttons and wheel; imgui-core already owns shared scancodes.

**Boundary verification**
- [ ] Record coordinate scaling, capture verdict and current-context behavior of both wrappers.
**Implementation and migration**
- [ ] Add scalar context/IO injection in existing imgui-core; migrate wrappers, retaining scaling/capture locally and no `draxul-types` dependency.
**Unit tests**
- [ ] Context-only key, modifier, text, button, wheel and null-context cases.
**Cross-platform validation**
- [ ] Verify Windows/macOS SDL input, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Clarify primitive versus wrapper responsibilities.
**Acceptance criteria**
- [ ] One injection mapping serves both wrappers without changing their capture behavior.

### kanban/pending/28 glyph-atlas-overlay-layout -refactor.md

# Use the glyph-atlas interface for overlay layout

**Priority:** P2 — layout tests currently need real font initialization.  
**Source:** `libs/draxul-gui/src/palette_renderer.cpp`  
**Proposed by:** Codex 12. **Owner:** one GUI/text agent.  
**Evidence:** palette/toast layout and occlusion use `TextService` for cluster resolution; `IGlyphAtlas` already exposes it, while tests skip when fonts are unavailable.

**Boundary verification**
- [ ] Trace cluster style and atlas-reset ordering through palette, toast and occlusion helper.
**Implementation and migration**
- [ ] Accept `IGlyphAtlas&` with explicit regular style; leave tooltip bitmap work on concrete text service.
**Unit tests**
- [ ] Fake-atlas clipping, continuation-cell and color cases; retain real-font integration.
**Cross-platform validation**
- [ ] Check font rendering on Windows/macOS, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Document interface use and reset sequencing.
**Acceptance criteria**
- [ ] Overlay layout can be tested without loading a font; atlas behavior remains intact.

### kanban/pending/29 kanban-interaction-policy -refactor.md

# Separate Kanban timing and selection policy from host effects

**Priority:** P2 — watcher, repeat, focus and preview timing share a render host.  
**Source:** `modules/kanban/draxul-kanban/src/kanban_host.cpp`  
**Proposed by:** Claude 40; Codex 8. **Owner:** one built-in Kanban agent.  
**Evidence:** host pump mixes deadlines and effects; host tests initialize fonts and use timed waits; completed focus/preview cards show interference risk.

**Boundary verification**
- [ ] Characterize focus loss, deferred preview, held-key repeat and watcher invalidation.
**Implementation and migration**
- [ ] Add value-oriented interaction/session owner in `draxul-kanban` taking timestamps/events and returning due commands; migrate timing first, pure selection transitions next. Keep monitor, disk, callbacks and drawing in host.
**Unit tests**
- [ ] Deterministic timeline tests in Kanban core; retain native monitor/host integration.
**Cross-platform validation**
- [ ] Verify native watchers on Windows/macOS, aggregate and smoke.
**Agent documentation and tooling**
- [ ] State pure policy versus host-effect ownership.
**Acceptance criteria**
- [ ] Policy tests use explicit time and preserve deferred preview after focus returns.

### kanban/pending/30 render-harness-core -refactor.md

# Separate CPU render harness from capture driver and demo host

**Priority:** P2 — scenario/diff work inherits renderer, font, host and NanoVG links.  
**Source:** `libs/draxul-render-test/CMakeLists.txt`  
**Proposed by:** Claude 38. **Owner:** one render-harness agent. **Coordinates with:** card 13.  
**Evidence:** one target compiles parser/diff, capture driver and `NanoVGDemoHost`; `render_test.h` includes concrete renderer and `AppOptions`.

**Boundary verification**
- [ ] Classify parser/diff/path/export, AppOptions mapping, capture and demo host dependencies.
**Implementation and migration**
- [ ] Add CPU `draxul-render-harness-core`; retain native capture driver and host-side demo target. Move scenario-to-AppOptions mapping to app/driver.
**Unit tests**
- [ ] Add narrow harness parser/diff tests and link-closure rejection; keep real render tests.
**Cross-platform validation**
- [ ] Check Windows/macOS render scenarios, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Update module map and narrow target command.
**Acceptance criteria**
- [ ] Harness core builds without renderer, host, font or NanoVG implementation.

### kanban/pending/31 nanovg-frame-driver -refactor.md

# Share NanoVG pass frame-driving sequence

**Priority:** P2 — core and plugin adapters repeat begin/scissor/translate/draw/end behavior.  
**Source:** `plugins/support/nanovg/src/plugin_nanovg_pass_internal.h`  
**Proposed by:** Claude 41, narrowed. **Owner:** one NanoVG adapter agent. **Related:** card 24.  
**Evidence:** core Vulkan/Metal passes and plugin NanoVG pass repeat frame sequence; plugin already has operation hooks, while native allocator ownership differs.

**Boundary verification**
- [ ] Pin viewport origin, callback consumption, preparation failure and teardown order.
**Implementation and migration**
- [ ] Add neutral frame driver inside backend support; migrate core/plugin adapters, retaining native resource and ABI translation.
**Unit tests**
- [ ] Test nonzero origin, failed preparation, draw order and callback lifetime via operation hooks.
**Cross-platform validation**
- [ ] Preserve Vulkan prepass and Metal encoder/drawable lifetime; run renders, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Document shared sequence versus native preparation.
**Acceptance criteria**
- [ ] Both adapters use one frame sequence without changing allocator or encoder ownership.

### plugins/megacity/kanban/pending/01 megacity-worker-ownership -refactor.md

# Own MegaCity grid and route workers privately

**Priority:** P1 — two worker lifecycles crowd the public host header and timing tests.  
**Source:** `plugins/megacity/product/draxul-megacity/include/draxul/megacity_host.h`  
**Proposed by:** Claude 43; Codex 3. **Owner:** one MegaCity agent.  
**Evidence:** header stores both thread/lock/generation sets; route worker calls host callback; tests use `#define private public` and sleeps.

**Boundary verification**
- [ ] Record cancellation, stale-generation, callback lifetime and nonblocking retirement behavior.
**Implementation and migration**
- [ ] Extract private grid and route worker owners with immutable requests/results; migrate grid then route. Keep algorithms in model and scene publication in host.
**Unit tests**
- [ ] Inject blocked builders for cancellation/latest-result/shutdown; retain host integration.
**Cross-platform validation**
- [ ] Check Windows/macOS thread teardown, `do.py test debug --megacity`, same-cache smoke and city/biology panes.
**Agent documentation and tooling**
- [ ] Update MegaCity guide at product root and focused test descriptions.
**Acceptance criteria**
- [ ] Host no longer exposes worker machinery; pump/input remain nonblocking.

### plugins/megacity/kanban/pending/02 megacity-frame-preparation -refactor.md

# Share MegaCity frame preparation and uniform layout

**Priority:** P1 — Vulkan/Metal uniform records and CPU preparation duplicate shader-facing state.  
**Source:** `plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_render_vk.cpp`  
**Proposed by:** Claude 24. **Owner:** one MegaCity renderer agent.  
**Evidence:** Vulkan GLM and Metal SIMD uniform structures/material packing and camera/AO setup are repeated in existing renderer target.

**Boundary verification**
- [ ] Record shader offsets, sizes, Y convention, shadow bias and per-backend differences.
**Implementation and migration**
- [ ] Add private neutral frame contract and pure preparation in existing renderer; migrate material, camera/AO and common shadow metadata in slices. Leave native upload/recording local.
**Unit tests**
- [ ] Deterministic material/frame values and ABI layout assertions through renderer test-internals.
**Cross-platform validation**
- [ ] Run Vulkan and Metal MegaCity goldens, `--megacity` aggregate and same-cache smoke.
**Agent documentation and tooling**
- [ ] Add shared/native split to MegaCity guide.
**Acceptance criteria**
- [ ] Both backends consume one CPU preparation path with intact shader ABI.

### plugins/megacity/kanban/pending/03 megacity-geometry-tests -refactor.md

# Isolate MegaCity geometry tests

**Priority:** P2 — pure geometry cases link the host and renderer suite.  
**Source:** `plugins/megacity/cmake/Tests.cmake`  
**Proposed by:** Claude 49, narrowed. **Owner:** one MegaCity test agent. **Depends on:** root card 02.  
**Evidence:** geometry tests use `draxul-geometry` but are in `draxul-test-megacity` with host/renderer links.

**Boundary verification**
- [ ] Classify geometry cases and their exact headers without assuming LCOV/config are pure.
**Implementation and migration**
- [ ] Register `draxul-test-megacity-geometry` against geometry only and include it in product scope/aggregate.
**Unit tests**
- [ ] Preserve case inventory and run the new narrow target.
**Cross-platform validation**
- [ ] Check both platform target closures, `--megacity` aggregate and smoke.
**Agent documentation and tooling**
- [ ] Correct MegaCity model/geometry test commands in its guide.
**Acceptance criteria**
- [ ] Geometry tests build without host or renderer dependencies and run in normal MegaCity scope.

### plugins/satview/kanban/pending/01 satview-test-boundaries -refactor.md

# Separate SatView CPU, scene and native tests

**Priority:** P2 — catalog/scene cases inherit runtime, renderer, SDL and Metal shader build.  
**Source:** `plugins/satview/cmake/Tests.cmake`  
**Proposed by:** Claude 49; Codex 5. **Owner:** one SatView test agent. **Depends on:** root card 02.  
**Evidence:** all C++ suites are globbed into one target; core, scene and services already have separate libraries.

**Boundary verification**
- [ ] Classify catalog, geodetic, scene, service, asset/shader and native cases.
**Implementation and migration**
- [ ] Add explicit CPU/scene/service targets and retain runtime/GPU target and Python catalog test in product aggregate.
**Unit tests**
- [ ] Preserve full case inventory and prove focused target closure.
**Cross-platform validation**
- [ ] Keep Metal texture tests and shader compilation native; run `--satview` aggregate and smoke on both OSes.
**Agent documentation and tooling**
- [ ] Update SatView focused test guidance.
**Acceptance criteria**
- [ ] CPU cases build without runtime renderer; scene may still inherit current render-contract headers until root card 21.

### plugins/satview/kanban/pending/02 satview-configuration-ownership -refactor.md

# Make SatView persistent configuration authoritative

**Priority:** P2 — roughly fifty settings are mirrored between runtime fields and config.  
**Source:** `plugins/satview/src/runtime/satview_runtime.cpp`  
**Proposed by:** Claude 25, with part of 48. **Owner:** one SatView runtime agent; serialize with card 03.  
**Evidence:** `current_config()` copies member fields out; `apply_config()` copies them back and triggers worker effects. Adapter retains unused triangle state.

**Boundary verification**
- [ ] Separate persistent settings from transient selection, camera, worker and pause authority.
**Implementation and migration**
- [ ] Store one normalized `SatViewConfig`; migrate coherent groups/panels and derive side effects from diff. Remove unused triangle fields/current-format serialization.
**Unit tests**
- [ ] Round trip, normalization, restored worker settings, pause and adapter storage.
**Cross-platform validation**
- [ ] Run `--satview` aggregate, native render cases and same-cache smoke on both OSes.
**Agent documentation and tooling**
- [ ] Update product configuration ownership notes.
**Acceptance criteria**
- [ ] No mirrored persistent setting remains; completed restore/pause regressions stay fixed.

### plugins/satview/kanban/pending/03 satview-scene-frame-composition -refactor.md

# Put SatView surface projection and frame description in scene

**Priority:** P2 — draw and picking repeat visibility/position choices.  
**Source:** `plugins/satview/src/runtime/satview_runtime.cpp`  
**Proposed by:** Claude 25; Codex 7. **Owner:** one SatView scene agent; serialize with card 02.  
**Evidence:** runtime helpers compose surface markers while picking repeats visibility, map wrap and placement; draw fills many pass setters.

**Boundary verification**
- [ ] Pin selected overrides, representatives, Moon/Mars placement, wrap and stream revision rules.
**Implementation and migration**
- [ ] Add value-only surface request/projection in `draxul-satview-scene`; make draw and picking consume it. Then replace related setter groups with a frame description, preserving partial upload revisions.
**Unit tests**
- [ ] Device-free surface and frame/revision cases, including wrapped picking.
**Cross-platform validation**
- [ ] Verify Vulkan/Metal SatView scenarios, `--satview` aggregate and smoke.
**Agent documentation and tooling**
- [ ] Update scene/runtime ownership guide.
**Acceptance criteria**
- [ ] One projection drives visual placement and identity selection without an all-at-once draw rewrite.

### plugins/rezonality/kanban/pending/01 rezonality-compiler-adapter -refactor.md

# Isolate Rezonality compiler execution and diagnostics

**Priority:** P2 — production decoding is tied to real process execution.  
**Source:** `plugins/rezonality/src/live_project.cpp`  
**Proposed by:** Codex 6. **Owner:** one Rezonality project agent.  
**Evidence:** source combines OS process runner, diagnostic parser, compiler and watcher; existing injection supplies already-decoded diagnostics.

**Boundary verification**
- [ ] Record arguments, temporary outputs, limits, cleanup and compiler error formats.
**Implementation and migration**
- [ ] Add private typed process-result/compiler adapter within project target; retain pipeline and watcher APIs.
**Unit tests**
- [ ] Recorded spawn/exit/timeout/output/diagnostic cases; retain real edit-break-repair integration.
**Cross-platform validation**
- [ ] Check Windows quoting and POSIX argument boundaries, `--rezonality` aggregate, goldens and smoke.
**Agent documentation and tooling**
- [ ] Update product project/compiler boundary notes.
**Acceptance criteria**
- [ ] Production diagnostic decoding is testable without invoking a compiler.

### plugins/rezonality/kanban/pending/02 rezonality-module-test-boundaries -refactor.md

# Reuse module implementation and partition contract tests

**Priority:** P2 — the contract target recompiles module/native sources and mixes test kinds.  
**Source:** `plugins/rezonality/cmake/Tests.cmake`  
**Proposed by:** Claude 45, narrowed. **Owner:** one Rezonality build/test agent.  
**Evidence:** test target recompiles plugin and selected backend, depends on the module, and runs serial; pure, in-process and dynamic-load cases share one suite.

**Boundary verification**
- [ ] Classify export, dynamic-load, edit and pure cases plus module/test link flags.
**Implementation and migration**
- [ ] Share private implementation objects between MODULE and in-process test; move pure cases to existing project/runtime/audio targets and narrow include ownership.
**Unit tests**
- [ ] Preserve ABI export and case inventory; keep only required dynamic tests serial.
**Cross-platform validation**
- [ ] Preserve host-resolved module SDL versus test-owned SDL and paired Vulkan/Metal sources; run aggregate, goldens and smoke.
**Agent documentation and tooling**
- [ ] Update product target map.
**Acceptance criteria**
- [ ] Native implementation compiles once per configuration and dynamic loading still works.

### plugins/scoreview/kanban/pending/01 scoreview-header-ownership -refactor.md

# Separate ScoreView pipeline/runtime headers and audio dependency

**Priority:** P2 — shared include root and pass-through audio link obscure ownership.  
**Source:** `plugins/scoreview/product/draxul-scoreview/CMakeLists.txt`  
**Proposed by:** Claude 46, narrowed. **Owner:** one ScoreView agent.  
**Evidence:** pipeline/runtime publish one include root; pipeline exposes audio, while runtime public microphone header truly includes listener types.

**Boundary verification**
- [ ] Classify public/private headers and exact audio includes.
**Implementation and migration**
- [ ] Split include ownership; make runtime directly own audio with visibility matching its public headers. Avoid unproven extra controllers.
**Unit tests**
- [ ] Compile minimal pipeline/runtime consumers plus existing ScoreView suites and standalone extraction.
**Cross-platform validation**
- [ ] Check macOS SDL module linkage, Windows Verovio DLL staging, `--scoreview` aggregate and smoke.
**Agent documentation and tooling**
- [ ] Add a short root product `AGENTS.md` with targets and platform gates.
**Acceptance criteria**
- [ ] Each consumer sees only owned headers and required audio links.

### plugins/pcbview/kanban/pending/01 pcbview-selection-ownership -refactor.md

# Move route visibility and picking into PCBView selection

**Priority:** P2 — autorouter exports presentation queries beside route generation.  
**Source:** `plugins/pcbview/src/autorouter.cpp`  
**Proposed by:** Claude 47, narrowed. **Owner:** one PCBView core agent.  
**Evidence:** autorouter defines layer visibility and connection picking; existing `selection.cpp` already owns distance policy and calls picking.

**Boundary verification**
- [ ] Pin route/wire/mount precedence, layers, vias, ties and tolerance.
**Implementation and migration**
- [ ] Move visibility and route picking to selection within existing `draxul-pcbview-core`; keep routing result generation in autorouter.
**Unit tests**
- [ ] Preserve hidden-route regression and picking geometry in existing core target.
**Cross-platform validation**
- [ ] Inspect runtime call sites on Windows/macOS; run `--pcbview` aggregate and same-cache smoke.
**Agent documentation and tooling**
- [ ] Add a short PCBView `AGENTS.md` for core/runtime/plugin boundaries and validation.
**Acceptance criteria**
- [ ] Selection owns all presentation picking without adding a GPU dependency.
