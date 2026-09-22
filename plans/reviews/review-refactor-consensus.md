# Refactor consensus

Accept all eight distinct proposals: two P1 and six P2. No P0 findings. Preserve the previously accepted cards; none of these proposals duplicates them.

The sole input was `INPUT_REVIEWS/01-openai-codex-gpt-6-astra.md`, whose SHA-256 matches the index. Its coverage was representative, not exhaustive. Three read-only verification workers checked coherent groups; all completed, and their supporting evidence was re-read. This is source-verified synthesis of one review, not agreement between independent review providers.

No files were changed, and no builds, tests, project binaries, or installation commands were run. The complete proposed cards below are for the runner to create. Prefixes `90`–`97` are unused in the current pending lane.

## Architecture and coverage

Current production boundaries are appropriate overall: App orchestrates; client/protocol/server libraries own headless Session behavior; PluginHost supplies the native-plugin ABI; products own their implementations; Vulkan and Metal remain backend-private.

Most accepted work therefore belongs **inside existing targets**. Only Rezonality audio warrants a new production static library.

| Reviewed proposal | Current evidence and synthesis |
|---|---|
| Client stream policy | Protocol decisions, transport writes, and publication remain interleaved in `RemoteSessionCoordinator`. Extract private policy; retain established recovery and projection owners. |
| UI control requests | The existing router handles reads, while App still implements mutation decoding and response policy. Extend that boundary without extracting another production library. |
| Session persistence dependency | `draxul-session-model` unnecessarily reaches application config and SDL. Replace its dependency with the existing config-support leaf. |
| Rezonality audio | Capture lifecycle lacks injectable operations and shares native-module test compilation. Add a product-private audio archive while keeping project option values independent of SDL. |
| Plugin storage | Storage policy/state remains embedded in PluginHost. Extract a private owner while preserving ABI and reload orchestration. |
| Tree-sitter parsing | Grammar extraction and directory scanning share one worker implementation. Extract synchronous parsing inside the existing library. |
| SatView CSV | Two lexical state machines duplicate behavior but use different diagnostics. Share tokenization while retaining domain-specific mapping. |
| Font dependencies | Opaque public APIs unnecessarily export native compile dependencies. Tighten visibility and declare white-box test requirements explicitly. |

Verification covered the cited implementation, callers, CMake relationships, tests, relevant documentation, scripts, root/nested guidance, and matching work across root/product pending, ice-box, and done lanes. It did not repeat the source review’s broad audit of unrelated rendering, terminal, compiler, DSP, or product algorithms.

## Reconciliation and exclusions

- **Already tracked:** retain `kanban/pending/80 rezonality-project-pipeline -refactor.md` through `kanban/pending/89 nanovg-paint-conversion -refactor.md`. Runtime/backend separation, renderer links, NanoVG adapters/conversion, process observers, installer, weather, Markdown metrics, and PCB selection receive no duplicate cards.
- **Already delivered:** stream capabilities, hot-reload storage overlays, font-style consolidation, and binary catalog framing are existing behavior. The accepted work isolates their implementation; it does not reimplement those capabilities.
- **Rejected interpretation:** making FreeType/HarfBuzz private does not eliminate their final-link requirements or compilation cost.
- **Rejected interpretation:** direct policy tests added to existing core/app/product executables do not make those executables’ build closures graphics-free.
- **Rejected expansion:** no new public plugin ABI, generalized CSV library, multi-file storage transaction, wholesale App rewrite, or separate production library for each helper.
- **Unresolved lead—no card:** `plugins/megacity/cmake/Tests.cmake:15–17` adds a module dependency to `draxul-test-app`; `tests/plugin_manager_tests.cpp:80–134` deliberately exercises City/Biology through the ABI. These checks enter the default core aggregate. The evidence establishes build cost, but does not settle their intended ownership. Reconcile the intended scope with `kanban/done/35 modular-test-targets -refactor.md` and `kanban/pending/37 product-plugin-repo-split -refactor.md` before proposing a migration.

## Existing task refinements

These are proposed amendments for the calling agent to merge, not replacement cards.

**`kanban/pending/80 rezonality-project-pipeline -refactor.md`**

Add to boundary verification: `src/live_project.h:3,30` consumes `AudioOptions`. Keep audio option/value records independently includable, without linking the CPU project target to SDL or capture implementation. Coordinate header ownership with the audio extraction below.

**`kanban/pending/81 rezonality-runtime-backend-boundary -refactor.md`**

Clarify that the controller owns audio orchestration through `AudioAnalyzer`; device opening, permission handling, capture sharing, buffering, and teardown belong to the proposed audio target. Serialize shared product CMake edits with the pipeline/audio owners.

**`kanban/pending/58 plugin-storage-error-preservation -bug.md`**

Use the private filesystem-operation seam introduced by the storage extraction for deterministic failure tests if it lands first. Otherwise, extract the already-fixed implementation. Preserve this card’s ownership of primary-error capture and separate cleanup errors.

**`kanban/pending/08 agent-guidance-label-validation -refactor.md`**

Add the new ownership/test mapping to its existing documentation audit. Correct MegaCity’s hardcoded `build-ninja-release` development example to the canonical selected Debug cache. Keep root `AGENTS.md` a pointer to `CLAUDE.md`; update product guidance locally.

**`kanban/pending/35 streamline-local-build-validation-workflow -refactor.md`**

Ensure new focused executables are included in both aggregate dependencies and `do.py` selection patterns. Current selection at `do.py:530–569` enumerates exact patterns; registration alone does not make a new suite run. Preserve the existing scope for supported Catch/label selection and zero-match detection.

All existing implementation and unavailable-platform checkboxes remain unchanged, including the outstanding Metal/manual-audio checks in `plugins/rezonality/kanban/done/005-audio-reactive-project.md`.

## Sequencing and ownership

1. **Small independent changes:** Session dependency correction and font visibility cleanup.
2. **P1 policy boundaries:** client policy and App routing, with separate owners. Neither requires the other.
3. **Independent product work:** Tree-sitter parsing and SatView tokenization.
4. **Coordinated lifecycle work:** Rezonality audio after agreeing value ownership with its pipeline/runtime owners; PluginStorage alongside or after the existing storage-error fix.

New compiled targets must respect `kanban/pending/00 internal-target-build-policy -refactor.md`. Existing-target mechanical extractions can establish their boundaries first. One integration owner should serialize shared `tests/CMakeLists.txt` and `do.py` edits.

Product implementation belongs in its product repository, followed by deliberate parent pointer adoption. Root card locations below follow this synthesis run’s explicit output contract.

## Repository guidance and validation

Keep shared rules in `CLAUDE.md`. Add only local ownership, lifetime, and test-entry details to subsystem documentation and the existing product guides. Update `docs/module-map.md` as boundaries land.

For implementation, `<cache>` means the selected existing Debug cache—normally `build-ninja-debug` on Windows and `build` on macOS. Focused commands below are iteration entry points; commands for new targets are explicitly proposed. Use `py` instead of `python3` on Windows.

Each completed slice needs one scope-appropriate `do.py test debug` aggregate and one same-cache smoke. Do not immediately precede it with redundant focused coverage. Register new tests exactly once and retain meaningful native integration coverage.

Static inspection cannot validate generated dependency closure, native linking, concurrency, microphone permissions, Windows/macOS behavior, or Vulkan/Metal rendering. Those remain implementation acceptance gates.

## Complete proposed work items

### kanban/pending/90 client-stream-policy -refactor.md

# Isolate client Session-stream policy

**Priority:** P1 — deterministic protocol testing has high leverage in concurrency-sensitive code.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 1.  
**Owner:** One client agent; delegate test cases only after the private value contract stabilizes.  
**Dependencies:** No dependency on another newly accepted refactor. Coordinate `kanban/pending/40 multiplexed-session-event-stream -feature.md` and `kanban/done/51 session-client-worker-wakeup -bug.md`.

**Evidence:** `libs/draxul-client/src/remote_session_coordinator.cpp:2437–2781` combines frame ordering, acknowledgement, command correlation, retry, fairness, publication, and transport writes. Policy state shares storage with threads/connections at `:3033–3077`. Existing coordinator tests construct listeners and workers at `tests/remote_session_coordinator_tests.cpp:552–590,884–926`.

**Boundary:** A private worker-owned `SessionStreamPolicy` inside existing `draxul-client`. Inputs are decoded protocol values, command/registration identities, observed revisions, timestamps, and transport outcomes. Outputs are typed admission/send/completion/retry/acknowledgement/fallback decisions. Coordinator retains connections, synchronization, registrations, projection cursors, waiter notification, and UI publication. Public APIs and existing control/protocol/session-model dependencies remain unchanged.

#### Boundary verification

- [ ] Inventory command IDs versus mutation IDs, queue limits, visibility generations, acknowledgement state, fairness, and fallback order.
- [ ] Preserve the separate recovery/projection owners and stream → poll → legacy negotiation.
- [ ] Characterize transport-success-dependent transitions; acknowledgement must clear only after a successful Update write.

#### Implementation and migration

- [ ] Extract command bookkeeping, correlation, one-retry policy, and fallback decisions first.
- [ ] Extract frame admission, acknowledgement requirements, fairness, and heartbeat decisions next.
- [ ] Keep side effects in coordinator adapters and preserve their order after each migration.
- [ ] Use opaque identities rather than owning `Entry`, connection, host, or renderer pointers.
- [ ] Preserve send-order fallback and reverse terminal requeue behavior.

#### Unit tests

- [ ] Add direct private-policy cases for stale epochs, skipped/duplicate serials, invalid response IDs, explicit-time heartbeat expiry, and obsolete registrations.
- [ ] Cover every Events batch requiring acknowledgement, failed acknowledgement writes, external/terminal fairness, and retry only after a newer revision.
- [ ] Retain native stream/poll/fallback integration tests.
- [ ] Build `cmake --build <cache> --config Debug --target draxul-client draxul-test-core --parallel`.
- [ ] During iteration run `ctest --test-dir <cache> -C Debug -R '^draxul-test-core-shard-' --parallel 4 --output-on-failure`; document a focused Catch tag for direct cases.

#### Cross-platform validation

- [ ] Exercise named-pipe and Unix-socket integration on Windows/macOS without changing transport ownership.
- [ ] Verify unchanged UI consumption on Vulkan/Metal and no graphics dependency enters policy.
- [ ] Run `python3 do.py test debug`, then `python3 do.py smoke debug --skip-build`.

#### Agent documentation/tooling

- [ ] Document private policy versus coordinator I/O/publication ownership and test-only access.
- [ ] Classify new tests once; keep headers private.
- [ ] Keep wakeup bug fixes in their existing card rather than silently combining them with extraction.

#### Acceptance criteria

- [ ] Ordering, fairness, retry, and acknowledgement decisions are testable without live listeners or sleeps.
- [ ] Public registration behavior, boundedness, worker ownership, and fallback ordering remain compatible.
- [ ] Each migration step builds; native integration coverage remains intact.
- [ ] No claim is made that the existing broad core test executable has become graphics-free.

### kanban/pending/91 app-control-request-boundary -refactor.md

# Complete the App control-request boundary

**Priority:** P1 — isolates frequently changed control contracts from App lifecycle and expensive fixtures.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 2.  
**Owner:** One App/control agent; serialize edits to `app/app.cpp`.  
**Dependencies:** No new-refactor prerequisite. Coordinate `kanban/pending/01 app-local-cmake-ownership -refactor.md` and `kanban/pending/85 agent-integration-installer -refactor.md` for source/test inventory edits.

**Evidence:** `app/app.cpp:5426–5918` mixes decoding, validation, native-session reporting, wait/input policy, mutations, and response construction. `app/control_request_router.h:13–26` already supplies a read boundary. Both belong to `draxul-app` in root `CMakeLists.txt:329–390`. Mutation tests require initialized App, fonts, fake graphics, asynchronous requests, and pumping (`tests/control_plane_tests.cpp:36–54,344–373`).

**Boundary:** Extend app-private routing with typed operation callbacks and value results. Router owns request policy and response mapping; App retains subsystem ownership and ordered focus/layout/input/persistence/frame effects. Preserve the existing read path. No new production library, public API, or renderer/window/transport dependency in callback contracts.

#### Boundary verification

- [ ] Inventory each request family, existing limits, errors, response shapes, and observable effect order.
- [ ] Keep local versus server routing and authoritative Session/projection responsibilities separate.
- [ ] Characterize `agent.start` activating an optional Space before validating later arguments (`app.cpp:5700–5727`); preserve that order during extraction.

#### Implementation and migration

- [ ] Define narrow callbacks for lookup/readback, focus, launch/restart, input, native-session acceptance, reload, and event reads.
- [ ] Extract wait and input policy first, then migrate remaining families individually.
- [ ] Retain App adapters for lifecycle and ordered UI effects; do not pass `App&` into policy.
- [ ] Preserve controller query implementations initially rather than expanding into their rewrite.
- [ ] Keep Session-navigation work in `kanban/ice-box/22 app-tab-session-controllers -refactor.md`.

#### Unit tests

- [ ] Add direct fake-operation tests for malformed requests, argument/text/key limits, rejected operations, input encoding, and response mapping.
- [ ] Cover wait generation replacement, default predicates/status precedence, and stale/duplicate native-session routes.
- [ ] Assert observable callback order and retain real endpoint/main-thread integration tests.
- [ ] Build `cmake --build <cache> --config Debug --target draxul-app draxul-test-app --parallel`.
- [ ] During iteration run `ctest --test-dir <cache> -C Debug -R '^draxul-test-app-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [ ] Preserve Windows/macOS key-byte behavior and native endpoint dispatch.
- [ ] Keep Vulkan/Metal effects behind App adapters; verify startup and focus behavior.
- [ ] Run `python3 do.py test debug`, then `python3 do.py smoke debug --skip-build`.

#### Agent documentation/tooling

- [ ] Document request-policy versus effect ownership in the module map.
- [ ] Keep new tests explicitly classified and coordinate installer test moves.
- [ ] Document direct-test runtime isolation without claiming the existing app test build is graphics-free.

#### Acceptance criteria

- [ ] Request-policy cases execute without App initialization, physical fonts, or transport pumping.
- [ ] App retains lifecycle authority and main-thread effects.
- [ ] Existing error codes, JSON responses, effect ordering, and public control behavior remain compatible.
- [ ] Every request-family migration remains independently buildable and reviewable.

### kanban/pending/92 session-config-leaf-dependency -refactor.md

# Remove Session persistence’s application-config dependency

**Priority:** P2 — a small dependency correction removes unnecessary application/SDL closure from headless consumers.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 3.  
**Owner:** One core dependency agent.  
**Dependencies:** None among newly accepted refactors; coordinate shared CMake audit edits.

**Evidence:** `libs/draxul-session-model/CMakeLists.txt:12–15` privately links `draxul-config`, whose private dependencies include SDL. `session_state.cpp:3–6,1163–1166` needs config-document/TOML support and `ConfigDocument::default_path()`. These already belong to `draxul-plugin-config-support` (`libs/draxul-plugin-support/CMakeLists.txt:47–52`; `src/config_document.cpp:38–42`). Current dependency guards inspect direct links; existing isolated consumers alone do not reject unwanted transitive dependencies.

**Boundary:** Replace the private application-config edge with existing `draxul-plugin-config-support`. Preserve Session public APIs and public agent/host-identity dependencies. Configuration schema/keybindings remain in `draxul-config`; path/document support remains in its existing leaf. No new production library.

#### Boundary verification

- [ ] Inventory Session implementation includes/symbols and the Session → protocol/client/server transitive closure.
- [ ] Confirm config-support supplies every required symbol without application configuration implementation.
- [ ] Record current directory selection, serialization, and durable replacement behavior.

#### Implementation and migration

- [ ] Replace only the obsolete target edge and retain necessary performance/JSON dependencies.
- [ ] Extend boundary validation to inspect the relevant transitive target closure, including static-library link-only edges.
- [ ] Reject application config, SDL implementation, and UI dependencies reaching headless Session/protocol/client/server owners.
- [ ] Handle aliases, cycles, and relevant generator-expression wrappers explicitly; avoid an unrelated graph-tool rewrite.

#### Unit tests

- [ ] Add meaningful configure fixtures proving an indirect forbidden edge fails and the intended leaf closure passes.
- [ ] Retain existing Session codec, checkpoint, default-path, and durability regressions.
- [ ] Build `cmake --build <cache> --config Debug --target draxul-session-model draxul-protocol-link-isolation draxul-client-link-isolation draxul-server-link-isolation --parallel`.
- [ ] Verify the isolated consumer link closures, not merely successful compilation through the broad test executable.

#### Cross-platform validation

- [ ] Check Windows/macOS default directories and durable I/O remain unchanged.
- [ ] Verify the headless closure gains neither Vulkan nor Metal dependencies.
- [ ] Run `python3 do.py test debug`, then `python3 do.py smoke debug --skip-build`.

#### Agent documentation/tooling

- [ ] Update dependency comments/module-map claims to describe the verified leaf dependency.
- [ ] Register configure fixtures in the existing validation flow and keep them platform-aware.
- [ ] Document target-closure isolation without promising graphics-SDK-free root configuration.

#### Acceptance criteria

- [ ] Session persistence no longer depends directly or transitively on application configuration or SDL implementation.
- [ ] Protocol/client/server isolation consumers retain valid links.
- [ ] Default paths, persisted formats, public APIs, and replacement semantics are unchanged.
- [ ] A regression through an indirect forbidden dependency is detected.

### kanban/pending/93 rezonality-audio-capture-boundary -refactor.md

# Isolate Rezonality audio capture and device operations

**Priority:** P2 — makes difficult device/lifecycle failures deterministic and removes native-renderer coupling from focused audio tests.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 4.  
**Owner:** One Rezonality audio agent; serialize product CMake edits with pipeline/runtime owners.  
**Dependencies:** `kanban/pending/00 internal-target-build-policy -refactor.md` for target policy; agree value ownership with `kanban/pending/80 rezonality-project-pipeline -refactor.md` and `kanban/pending/81 rezonality-runtime-backend-boundary -refactor.md`.

**Evidence:** `plugins/rezonality/src/audio_analysis.cpp:164–365` combines permission polling, device opening, subscriber visibility, buffering, teardown, and a static capture registry. Synthetic/Silent tests bypass capture (`tests/rezonality_plugin_contract_tests.cpp:348–378`). Product and test CMake duplicate audio compilation alongside native rendering. `src/live_project.h:3,30` also consumes `AudioOptions`.

**Boundary:** Product-private `draxul-rezonality-audio STATIC`, preserving `AudioAnalyzer` and its value contracts. Capture/device/permission operations, registry, synchronization, DSP, and native handles stay private. Dependencies are standard C++, SDL requirements, and platform permission support—not renderer, Assimp, compiler, or App. Keep option/value records independently includable.

#### Boundary verification

- [ ] Freeze value/header ownership so the CPU project pipeline does not link audio implementation or SDL.
- [ ] Inventory exact-device sharing, visibility transitions, permission/open outcomes, generation semantics, and shutdown ordering.
- [ ] Record Apple module versus executable SDL linkage before moving sources.

#### Implementation and migration

- [ ] Move audio and selected permission sources into the static target; remove duplicate compilation.
- [ ] Add the target to product `PRODUCT_TARGETS`, retaining PIC and product sanitizer/coverage/MSVC policy.
- [ ] Add private injectable device, permission, and wait operations plus an explicitly owned test registry.
- [ ] Retain production sharing and existing `AudioAnalyzer` callers.
- [ ] Add proposed `draxul-test-rezonality-audio` with no renderer/App dependency.

#### Unit tests

- [ ] Cover permission pending/granted/denied; initialization, device lookup, open, and resume failure.
- [ ] Cover cancellation during opening, exactly-once cleanup, same/distinct-device sharing, and visibility reference counts.
- [ ] Cover final-hidden pause/clear, backlog limits, and existing Synthetic/Silent outputs.
- [ ] Retain real module/edit/reload and synthetic-audio render coverage.
- [ ] Enable `cmake --build <cache> --config Debug --target draxul-test-rezonality-audio --parallel`.
- [ ] Enable `ctest --test-dir <cache> -C Debug -R '^draxul-test-rezonality-audio-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [ ] Preserve Apple module header-only SDL linkage and host symbol resolution; test executables supply SDL implementation.
- [ ] Preserve ARC/AVFoundation permissions and Windows SDL/stub behavior.
- [ ] Keep Vulkan/Metal analysis texture contents and generations unchanged.
- [ ] Run `python3 do.py test debug --rezonality`, then `python3 do.py smoke debug --skip-build`.
- [ ] Leave unavailable manual microphone and Metal golden checks explicitly pending.

#### Agent documentation/tooling

- [ ] Register the focused suite in product aggregates, `do.py` patterns, and runner selection tests.
- [ ] Preserve absent-product conditional registration and classify moved tests once.
- [ ] Update Rezonality guidance/module map; commit product changes separately and adopt the pointer deliberately.

#### Acceptance criteria

- [ ] Audio implementation has one library owner.
- [ ] Capture failure/lifecycle tests require neither physical input nor App/native rendering.
- [ ] The CPU project target remains independent of SDL implementation.
- [ ] Existing sharing, visibility, shutdown, module loading, and rendered audio behavior remain compatible.

### kanban/pending/94 plugin-storage-private-owner -refactor.md

# Give PluginHost storage a private owner

**Priority:** P2 — separates filesystem policy from native rendering and reload lifecycle while enabling deterministic failure tests.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 5.  
**Owner:** One core host/storage agent.  
**Dependencies:** Coordinate `kanban/pending/58 plugin-storage-error-preservation -bug.md`; no dependency on another newly accepted refactor. Coordinate source ownership with `kanban/done/02 host-layer-static-libraries -refactor.md`.

**Evidence:** `libs/draxul-host/src/plugin_host.cpp:63–85,156–203,489–519,734–778,898–1002` owns paths, validation, buffering, replacement, and overlays. State resides in `plugin_host.h:157–172`. Tests access it through loaded modules and initialized hosts (`tests/plugin_manager_tests.cpp:350–389,898–935`).

**Boundary:** Private `PluginStorage` sources inside existing `draxul-host`, with path/scope queries, JSON read/write/remove, and overlay begin/commit/discard operations. PluginHost retains ABI callbacks, pointer/thread/generation checks, and reload orchestration. Filesystem/JSON details stay private; no SDK or PluginSupport expansion and no new production library.

#### Boundary verification

- [ ] Pin path layout, scope/key validation, result precedence, JSON limits, and NUL-inclusive buffer sizing.
- [ ] Pin staged writes/removals, disk fallback, discard, and best-effort partial commit.
- [ ] Preserve App’s cohort use of `PluginHost::finalize_reload_storage()`; storage must not own activation policy.

#### Implementation and migration

- [ ] Mechanically move storage state, path calculation, and operations into the collaborator.
- [ ] Delegate ABI operations after existing host checks.
- [ ] Introduce narrow private filesystem-operation injection for failure tests.
- [ ] Preserve or adopt the fix from the existing storage-error card without duplicating its ownership.
- [ ] Keep each migration buildable and preserve reload sequencing.

#### Unit tests

- [ ] Test scoped paths, invalid/unavailable scopes, JSON validity/limits, missing files, and two-call buffer behavior directly.
- [ ] Test overlay visibility, tombstones, discard, successful commit, failure, and current partial publication.
- [ ] Reuse the existing bug card’s injected open/write/flush/replace/cleanup assertions.
- [ ] Retain real-module, stale-callback, wrong-thread, and rollback integration coverage.
- [ ] Build `cmake --build <cache> --config Debug --target draxul-host draxul-test-app --parallel`.
- [ ] During iteration run `ctest --test-dir <cache> -C Debug -R '^draxul-test-app-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [ ] Preserve Windows `MoveFileExW` flags, POSIX rename, UTF-8 paths, and platform storage roots.
- [ ] Preserve reload/GPU lifetime ordering on Vulkan and Metal.
- [ ] Run `python3 do.py test debug --products`, then `python3 do.py smoke debug --skip-build`.

#### Agent documentation/tooling

- [ ] Document private storage versus host lifecycle ownership and partial-commit semantics.
- [ ] Provide explicit test-only access without exporting implementation headers.
- [ ] Document that direct cases avoid module initialization, while the existing app test target retains its build closure.

#### Acceptance criteria

- [ ] Storage policy is directly testable without loading a plugin.
- [ ] ABI status codes, paths, limits, callbacks, and reload outcomes remain compatible.
- [ ] Existing overlay behavior is preserved; no multi-file transaction rewrite is introduced.
- [ ] Each implementation responsibility has one owner and native integration tests remain intact.

### kanban/pending/95 treesitter-source-parser -refactor.md

# Separate Tree-sitter source parsing from directory scanning

**Priority:** P2 — removes filesystem/thread fixtures from grammar cases while preserving immutable scan publication.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 6.  
**Owner:** One MegaCity parser/scanner agent.  
**Dependencies:** No newly accepted production-boundary prerequisite; `kanban/pending/00 internal-target-build-policy -refactor.md` applies to the proposed focused test target.

**Evidence:** `plugins/megacity/product/draxul-treesitter/src/treesitter.cpp:504–782` combines parser/query resources, traversal, reading, progress, parsing, and publication. Grammar tests start workers on temporary files (`plugins/megacity/tests/treesitter_tests.cpp:48–78`). Existing `draxul-treesitter STATIC` owns Tree-sitter core/grammar and support-type dependencies.

**Boundary:** A synchronous private parser/resource owner inside `draxul-treesitter`: source text plus normalized relative path → existing `ParsedFile` with explicit success/skip status. Keep native parser/query/tree handles private. Scanner retains reading, filtering, cancellation, progress, and publication; `SemanticSourceController` continues consuming unchanged snapshots.

#### Boundary verification

- [ ] Capture record order, path normalization, query reuse/fallback, parse-error records, and progress semantics.
- [ ] Preserve null-tree skip behavior and increment parsed count only after successful parsing (`treesitter.cpp:582–588`).
- [ ] Keep downstream model/layout work in `plugins/megacity/kanban/pending/05 megacity-model-layout-routing-library -refactor.md`.

#### Implementation and migration

- [ ] Extract RAII parser/query ownership with one reusable parser context per scan.
- [ ] Move per-source extraction into the synchronous operation.
- [ ] Delegate the worker’s parsing step without changing traversal or publication.
- [ ] Move grammar-focused cases to in-memory fixtures.
- [ ] Add proposed product-owned `draxul-test-megacity-parser`, linked only to the parser library and test support; retain scanner integration cases in their existing suite.

#### Unit tests

- [ ] Cover functions/methods, forward declarations, fields/references, inheritance, abstract classes, includes, and malformed source.
- [ ] Preserve optional-query fallback and successful-versus-skipped parse accounting.
- [ ] Retain restart, path-filter, cancellation, completion, and semantic-consumer integration tests.
- [ ] Keep deferred cap/mixed-file coverage in `plugins/megacity/kanban/ice-box/13 codebasescanner-parse-error-cap -test.md`.
- [ ] Enable `cmake --build <cache> --config Debug --target draxul-treesitter draxul-test-megacity-parser --parallel`.
- [ ] Enable `ctest --test-dir <cache> -C Debug -R '^draxul-test-megacity-parser-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [ ] Compare record/path behavior on Windows/macOS and preserve downstream City/Biology inputs on Vulkan/Metal.
- [ ] Keep the parser free of renderer/host dependencies and preserve MegaCity enabled/disabled build support.
- [ ] Run `python3 do.py test debug --megacity`, then `python3 do.py smoke debug --skip-build`.

#### Agent documentation/tooling

- [ ] Partition product test globs exactly once and add the focused target to product aggregates and `do.py` selection.
- [ ] Update nested guidance with synchronous-parser versus scanner ownership and the selected Debug cache.
- [ ] Commit implementation in MegaCity and adopt its pointer deliberately.

#### Acceptance criteria

- [ ] Grammar tests require no temporary source files, worker polling, or graphics initialization.
- [ ] Parser resources remain private and reused efficiently.
- [ ] Scanner API, immutable snapshots, progress/completion semantics, and parsed records remain compatible.
- [ ] The focused parser test build excludes host and renderer targets.

### kanban/pending/96 satview-csv-tokenizer -refactor.md

# Consolidate SatView’s CSV tokenizer

**Priority:** P2 — one lexical implementation prevents divergence across four catalog entry points.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 7.  
**Owner:** One SatView core/catalog agent.  
**Dependencies:** None among accepted refactors.

**Evidence:** `plugins/satview/src/core/satview_catalog.cpp:715–803` and `src/core/satview_surface_catalog.cpp:20–112` duplicate quoted-field/line-ending state machines. Consumers appear at catalog `:1174,1293,1415` and surface `:249`. Both sources already belong to `draxul-satview-core STATIC`. Existing SATCAT quote/CRLF coverage is at `tests/satview_catalog_tests.cpp:170–198`.

**Boundary:** One private tokenizer within existing `draxul-satview-core`, using standard-library rows and typed lexical errors. Callers retain diagnostics, headers, numeric parsing, filtering, and record validation. No new production library or public API. The completed binary-container refactor does not cover this text tokenizer.

#### Boundary verification

- [ ] Compare both implementations’ quotes, escaped quotes, embedded newlines, CRLF/lone CR, final fields, empty rows, and error output.
- [ ] Record intentionally different generic versus surface error messages.
- [ ] Preserve accumulated rows on failure and each caller’s handling of them.

#### Implementation and migration

- [ ] Characterize current lexical behavior before moving it.
- [ ] Extract the common implementation and migrate the generic catalog callers first.
- [ ] Migrate surface parsing with explicit translation back to its current diagnostics.
- [ ] Remove duplicate code after all four entry points delegate.
- [ ] Keep numeric-validation and domain-record policies in their existing owners.

#### Unit tests

- [ ] Add one lexical matrix covering empty input, delimiters, escaped/embedded quotes, embedded newlines, CRLF/lone CR, trailing blank rows, malformed quotes, and partial output.
- [ ] Retain caller-level checks for all four entry points and exact diagnostic mapping.
- [ ] Give tests explicit private-header access within the existing product suite.
- [ ] Build `cmake --build <cache> --config Debug --target draxul-satview-core draxul-test-satview --parallel`.
- [ ] During iteration run `ctest --test-dir <cache> -C Debug -R '^draxul-test-satview-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [ ] Preserve byte/line-ending behavior on Windows/macOS and unchanged records supplied to Vulkan/Metal.
- [ ] Keep the helper free of SDL/GPU dependencies.
- [ ] Run `python3 do.py test debug --satview`, then `python3 do.py smoke debug --skip-build`.

#### Agent documentation/tooling

- [ ] Document tokenizer ownership separately from catalog semantics and binary framing.
- [ ] Keep tests in the existing product classification and record a focused Catch tag.
- [ ] Commit in SatView and adopt its pointer deliberately.

#### Acceptance criteria

- [ ] All four consumers use one lexical implementation.
- [ ] Diagnostics, partial output, empty-row behavior, and domain validation remain compatible.
- [ ] Lexical cases require no graphics initialization; no graphics-free claim is made for the existing broad product test build.
- [ ] Each migration step builds independently.

### kanban/pending/97 font-native-dependency-visibility -refactor.md

# Make font-native compile dependencies private

**Priority:** P2 — closes an unnecessary compile-interface leak with limited production churn.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 8.  
**Owner:** One font/build agent.  
**Dependencies:** No dependency on another newly accepted refactor; coordinate `kanban/pending/00 internal-target-build-policy -refactor.md` for the new build-only consumer.

**Evidence:** `libs/draxul-font/CMakeLists.txt:13–15` exports FreeType/HarfBuzz publicly. `text_service.h:86–88` and `rich_text_service.h:81–83` hide implementation through opaque pointers; native types live in private `src/font_engine.h`. Font/style/raster tests reach private headers through source-relative includes and inherit native compile requirements implicitly.

**Boundary:** Keep `draxul-types` public; make FreeType/HarfBuzz private to `draxul-font`. Add explicit test-only font-internals access carrying private headers and their native compile requirements. Preserve public text APIs, plugin consumers, and native final-link dependencies. No new production library.

#### Boundary verification

- [ ] Inventory all public font headers and confirm no native types/macros require public propagation.
- [ ] Find every white-box consumer, including font, resolver, raster-error, style-model, and box-drawing tests.
- [ ] Record production compile usage separately from static final-link requirements.

#### Implementation and migration

- [ ] Introduce a test-only interface target for required private includes and native dependencies.
- [ ] Migrate white-box includes and link their owning test target explicitly.
- [ ] Change production FreeType/HarfBuzz visibility to private.
- [ ] Add proposed `draxul-font-link-isolation`, consuming public headers through only `draxul-font`.
- [ ] Keep implementation headers out of installed/public APIs.

#### Unit tests

- [ ] Retain font/style/fallback/raster-error coverage without duplicating implementation-mirroring tests.
- [ ] Verify the isolated public consumer builds/links and does not inherit native compile include requirements.
- [ ] Verify white-box consumers receive those requirements only through explicit test access.
- [ ] Enable `cmake --build <cache> --config Debug --target draxul-font-link-isolation draxul-test-core --parallel`.
- [ ] During iteration run `ctest --test-dir <cache> -C Debug -R '^draxul-test-core-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [ ] Verify MSVC and Apple Clang compilation and retained native final links.
- [ ] Build affected plugin/font consumers and preserve Vulkan/Metal text rendering.
- [ ] Run `python3 do.py test debug --products`, then `python3 do.py smoke debug --skip-build`; retain affected font/style render checks.

#### Agent documentation/tooling

- [ ] Register the build-only consumer in the appropriate aggregate.
- [ ] Document public font contracts versus private native/test requirements.
- [ ] Preserve the completed scope and outstanding validation of `kanban/done/30 font-style-model -refactor.md`.

#### Acceptance criteria

- [ ] Production consumers no longer inherit FreeType/HarfBuzz compile interfaces through `draxul-font`.
- [ ] White-box tests declare their private dependencies explicitly.
- [ ] Public font APIs, shaping/rasterization behavior, and final links remain compatible.
- [ ] The report distinguishes compile-interface cleanup from dependency build-cost reduction.

## Final target/module map

Arrows mean “depends on.” Bracketed names are private collaborators, not separate production libraries.

| Owner | Intended dependency direction |
|---|---|
| Existing `draxul-client` | `[SessionStreamPolicy]` uses protocol values; coordinator owns control transport and publication |
| Existing `draxul-app` | Router → typed App operations; App retains lifecycle/UI dependencies |
| Existing `draxul-session-model` | → existing `draxul-plugin-config-support`; remove → `draxul-config` |
| **New `draxul-rezonality-audio STATIC`** | → platform-appropriate SDL requirements and permission support; plugin/runtime → audio |
| Existing `draxul-host` | PluginHost → `[PluginStorage]` → private filesystem/JSON implementation |
| Existing `draxul-treesitter STATIC` | Scanner → `[SourceParser]` → Tree-sitter core/grammar |
| Existing `draxul-satview-core STATIC` | Catalog parsers → `[CSV tokenizer]` |
| Existing `draxul-font` | Public → types; private → FreeType/HarfBuzz; tests explicitly → font internals |

Previously accepted additions remain unchanged: `draxul-rezonality-project STATIC`, `draxul-plugin-nanovg-support STATIC`, `draxul-agent-integration STATIC`, and `draxul-weather STATIC`. Rezonality project values must not depend on audio implementation; runtime orchestrates the project/audio/backend boundaries. No new dependency between the eight follow-up refactors is required.

<model>gpt-6-astra</model>
