Eight distinct refactoring findings survived verification against implementation, callers, tests, CMake, and the root/product Kanban lanes. No P0 findings.

This was a static, representative whole-repository review using only `repomix-output.xml`. Four read-only subagents used the same model at high reasoning effort; all completed, and their accepted evidence was re-read by the coordinator. Nothing was edited, built, installed, or executed from the project. Coverage is not exhaustive; specific gaps appear below.

**1. P1 — Separate client stream policy from transport and publication**

- **Location:** `libs/draxul-client/src/remote_session_coordinator.cpp:2437–2781,3033–3077`; `libs/draxul-client/CMakeLists.txt:1–20`.
- **Problem:** Frame ordering, acknowledgements, command correlation, fairness, and revision-conflict retries share one implementation with connection threads, terminal mailboxes, and UI wakes. Deterministic decisions directly perform transport/publication operations. Corresponding tests construct real listeners and worker threads (`tests/remote_session_coordinator_tests.cpp:552–590,884–926`).
- **Boundary and dependencies:** Add a private stream-policy collaborator within existing `draxul-client`. Inputs are decoded protocol values, command intents, and explicit timestamps; outputs are send, completion, and fallback decisions. Keep connections, threads, registrations, and publication in the coordinator.
- **Migration:** Extract command bookkeeping first, then frame admission and acknowledgement decisions. Adapt existing calls without changing the public registration API, worker ownership, or transport modes.
- **Testing and agent benefit:** Deterministic ordering, fairness, correlation, and retry fixtures would complement existing native transport tests. Protocol-policy work could proceed separately from mailbox and shutdown changes.
- **Risks/prerequisites:** Preserve command identity, visibility generations, fallback ordering, and named-pipe/Unix-socket behavior. Coordinate with `kanban/pending/40 multiplexed-session-event-stream -feature.md`; this proposal concerns private implementation isolation, not its tracked capabilities or legacy-path migration.

**2. P1 — Complete the UI control-request boundary**

- **Location:** `app/app.cpp:5426–5918`; `app/control_request_router.h:13–26`; `app/control_request_router.cpp:148`; root `CMakeLists.txt:329–390`.
- **Problem:** Read requests have a router, but mutation validation, native-session reporting, agent wait policy, input encoding, and response construction remain embedded in `App`. Testing these policies currently requires App initialization, physical fonts, fake graphics, asynchronous transport, and event pumping (`tests/control_plane_tests.cpp:36–54,344–445`).
- **Boundary and dependencies:** Extend the app-private routing boundary with narrow typed operation callbacks. The router owns request decoding, validation, and response mapping; App retains subsystem ownership, focus/layout refresh, rendering, and lifecycle effects. Keep both in `draxul-app`; a new production library is unnecessary.
- **Migration:** Move one request family at a time, beginning with validation and wait/input policy. Preserve the existing read router and delegate effects through explicit callbacks.
- **Testing and agent benefit:** Exercise malformed requests, argument limits, wait outcomes, and rejected operations directly with fake callbacks. Retain endpoint integration tests. Control-contract changes would stop colliding with the main application loop.
- **Risks/prerequisites:** Preserve main-thread execution, error codes, local/server identity distinctions, and effect ordering. Keep the separately tracked Session-projection extraction outside this package.

**3. P2 — Remove the obsolete application-config dependency from Session persistence**

- **Location:** `libs/draxul-session-model/CMakeLists.txt:12–15`; `libs/draxul-session-model/src/session_state.cpp:3–6,1163–1166`; `libs/draxul-config/CMakeLists.txt:16–24`.
- **Problem:** Session persistence links the full application configuration library, bringing configuration implementation and SDL into the headless dependency closure. Its required `ConfigDocument::default_path()` and TOML support already belong to `draxul-plugin-config-support` (`libs/draxul-plugin-support/CMakeLists.txt:47–52`, `src/config_document.cpp:38–42`).
- **Boundary and dependencies:** Replace the private `draxul-config` edge with existing `draxul-plugin-config-support`. Preserve public agent/host-identity dependencies and all persistence APIs.
- **Migration:** Change the dependency, then strengthen boundary validation to inspect transitive dependencies. Current checks reject only direct UI links (`cmake/CheckDependencyBoundaries.cmake:62–70`).
- **Testing and agent benefit:** Extend the existing client/server/protocol isolation checks (`tests/CMakeLists.txt:442–449`) and retain checkpoint regressions. Session changes would no longer pull application keybinding/schema code and SDL through this edge.
- **Risks/prerequisites:** Preserve default directories and durable I/O on both platforms. No path or serialization rewrite is needed.

**4. P2 — Isolate Rezonality audio capture and provide injectable device operations**

- **Location:** `plugins/rezonality/src/audio_analysis.cpp:164–365`; `src/audio_analysis.h:37–51`; `CMakeLists.txt:77–130`; `cmake/Tests.cmake:5–25,64–84`.
- **Problem:** `CaptureService` combines permission polling, an opener thread, SDL operations, subscriber visibility, buffering, and teardown behind a hidden static registry. Existing Synthetic/Silent tests bypass that service (`tests/rezonality_plugin_contract_tests.cpp:348–378`). The test target also recompiles the native renderer implementation and depends on the application.
- **Boundary and dependencies:** Create product-private `draxul-rezonality-audio STATIC`, preserving `AudioAnalyzer` and its value types. Keep capture operations, permission adapters, registry, and native handles private. Dependencies are standard C++, SDL, and platform permission support; no GPU renderer, Assimp, or compiler dependency is necessary.
- **Migration:** Move audio/permission sources into the target, remove duplicate compilation, then introduce injectable operations and an isolated registry fixture. Production can retain its shared registry.
- **Testing and agent benefit:** Deterministically test permission/open failures, shutdown during opening, shared subscribers, final-hidden pause/clear, and backlog handling. Audio changes gain an independent build/test boundary.
- **Risks/prerequisites:** Preserve synchronization and exact-device sharing. On macOS, retain module-side SDL header-only linkage and host symbol resolution; test executables still supply SDL. Coordinate CMake edits with tracked Rezonality pipeline/runtime work, which does not provide this capture seam.

**5. P2 — Give PluginHost storage a private owner**

- **Location:** `libs/draxul-host/src/plugin_host.cpp:63–85,176–203,489–519,734–778,898–1002`; `include/draxul/plugin_host.h:157–176`; `CMakeLists.txt:1–45`.
- **Problem:** PluginHost owns storage paths, key/JSON validation, filesystem replacement, buffer sizing, and reload overlays alongside callback generations, native rendering, and instance lifecycle. Storage tests access those responsibilities through dynamically loaded fixtures and host initialization (`tests/plugin_manager_tests.cpp:350–389,898–934`).
- **Boundary and dependencies:** Add private `PluginStorage` sources within existing `draxul-host`, exposing scoped read/write/remove and overlay begin/commit/discard operations. Keep ABI callbacks, thread checks, generation validation, and reload orchestration on PluginHost. Filesystem, JSON, and platform replacement details stay private.
- **Migration:** Mechanically move storage state and operations, delegate callbacks, then introduce filesystem-operation injection for failure tests.
- **Testing and agent benefit:** Test validation, overlay visibility, rollback, and publication failures without loading a native module. Storage and GPU/lifecycle work gain separate ownership.
- **Risks/prerequisites:** Preserve Windows `MoveFileExW` versus POSIX rename behavior, UTF-8 paths, status codes, and existing partial-commit semantics. This is separate from implemented journaling in pending hot-reload card 41; coordinate with the storage-error preservation bug card 58.

**6. P2 — Separate Tree-sitter parsing from directory scanning**

- **Location:** `plugins/megacity/product/draxul-treesitter/src/treesitter.cpp:504–782`; `include/draxul/treesitter.h:74–106`; `CMakeLists.txt:3–14`.
- **Problem:** `scan_thread()` owns parser/query resources, filesystem traversal, source reading, progress, cancellation, symbol extraction, and publication. Even grammar-only tests write temporary files and start/poll a worker (`plugins/megacity/tests/treesitter_tests.cpp:48–78`; `tests/support/codebase_snapshot_wait.h:23–45`).
- **Boundary and dependencies:** Add a synchronous product-private source parser inside existing `draxul-treesitter`: source text plus relative path → existing `ParsedFile`. Keep Tree-sitter handles and queries private; the scanner retains traversal and publication. `SemanticSourceController` continues consuming immutable snapshots unchanged.
- **Migration:** Extract reusable parser resource ownership and per-file parsing, delegate the scan loop, then migrate grammar-focused tests to in-memory inputs.
- **Testing and agent benefit:** Symbol, field, inheritance, and abstract-class cases become deterministic and independently testable. Parser and scanner-lifecycle changes gain separate source ownership.
- **Risks/prerequisites:** Preserve query reuse, best-effort fallback, parse-error records, normalized paths, and progress semantics. Keep restart/path-filter integration tests and both platform builds.

**7. P2 — Consolidate SatView’s duplicated CSV tokenizer**

- **Location:** `plugins/satview/src/core/satview_catalog.cpp:715–803`; `src/core/satview_surface_catalog.cpp:20–112`; `CMakeLists.txt:31–51`.
- **Problem:** Two matching state machines separately implement quoted fields, escaped quotes, embedded newlines, CRLF, trailing empty rows, and syntax errors. Four catalog entry points depend on this contract (`satview_catalog.cpp:1174,1293,1415`; `satview_surface_catalog.cpp:249`).
- **Boundary and dependencies:** One private tokenizer within existing `draxul-satview-core`, using only standard-library values. Keep domain-specific diagnostics, headers, numeric parsing, filtering, and record validation in each caller.
- **Migration:** Characterize current lexical behavior, extract one implementation, then migrate the second with diagnostic translation. No new production library or public API is needed.
- **Testing and agent benefit:** One lexical test matrix protects every consumer. Existing quote/CRLF coverage exercises the satellite path (`tests/satview_catalog_tests.cpp:170–198`), while surface tests principally cover domain rules. Syntax maintenance would have one owner.
- **Risks/prerequisites:** Preserve error wording, partial output, and empty-row behavior. Do not merge deliberately different numeric policies. The completed binary catalog-container refactor does not cover this textual tokenizer.

**8. P2 — Make font-native compile dependencies private**

- **Location:** `libs/draxul-font/CMakeLists.txt:13–15`; `include/draxul/text_service.h:86–88`; `include/draxul/rich_text_service.h:81–83`.
- **Problem:** FreeType and HarfBuzz are PUBLIC dependencies despite opaque public APIs. Native types live in private headers. White-box tests reach those headers using source-relative paths (`tests/font_tests.cpp:7–8`, `font_resolver_style_tests.cpp:7–9`, `glyph_raster_error_tests.cpp:3–6`) and implicitly inherit native compile requirements.
- **Boundary and dependencies:** Keep `draxul-types` PUBLIC; make FreeType/HarfBuzz PRIVATE. Give white-box tests explicit font-internals access carrying the private headers and required compile dependencies.
- **Migration:** Declare test requirements, migrate private includes, then tighten production dependency visibility.
- **Testing and agent benefit:** Add an isolated public-header consumer and retain font/style/raster tests. This prevents accidental production reliance on native include paths and makes test ownership explicit.
- **Risks/prerequisites:** Validate Windows/macOS and plugin consumers. Static final links still require FreeType/HarfBuzz; this change reduces compile-interface leakage, not their build cost or final link closure.

**Unresolved lead — product integration tests in the core app suite**

`plugins/megacity/cmake/Tests.cmake:15–17` adds its module to `draxul-test-app`, whose product-specific checks appear in `tests/plugin_manager_tests.cpp:80–170`. This warrants clarification of intended core-versus-product validation ownership. Acceptance would require reconciling those deliberate ABI integration checks with existing modular-test and product-repository cards, then establishing the resulting build/test closure. No new work card is recommended from this lead.

Already tracked App-shell, host/Neovim, Kanban, renderer/NanoVG, weather, installer, Markdown-metrics, product decomposition, guidance, and validation-workflow work is excluded.

**Proposed module/target map**

| Existing owner | Proposed boundary |
|---|---|
| `draxul-client` | Private stream policy; existing coordinator owns I/O/publication |
| `draxul-app` | Request router with typed operation callbacks |
| `draxul-session-model` | Private dependency on existing config-support leaf |
| Rezonality plugin | New product-private `draxul-rezonality-audio` |
| `draxul-host` | Private `PluginStorage` |
| `draxul-treesitter` | Synchronous private source parser |
| `draxul-satview-core` | Private shared CSV tokenizer |
| `draxul-font` | Private native dependencies; explicit test internals |

**Parallel work packages and sequencing**

The Session dependency correction and font visibility cleanup are small independent packages. MegaCity parsing, SatView CSV, and Rezonality audio can proceed independently in their product repositories. Assign separate owners to client stream policy, App routing, and PluginHost storage; coordinate their shared test-CMake edits and the relevant pending migration cards.

Preserve the existing C ABI, PluginSupport allowlist, product-owned payloads, renderer-private backends, renderer-free terminal state, immutable worker snapshots, server façade/private test access, and real transport/module integration tests.

**Coverage**

| Area | Representative paths/flows examined | Inspector | Remaining gaps |
|---|---|---|---|
| App/configuration | App initialization/reload/pump/shutdown, control routing, controllers, config schema, lifecycle tests | Coordinator | Not every action or configuration branch |
| Client/server/control | Discovery, recovery, stream/poll delivery, request dispatch, checkpoint ownership, platform transport | Server/client worker + coordinator | Native security and concurrency paths sampled |
| Terminal/Neovim/input | Host→terminal core, remote presentation, Nvim queues/lifecycle, PTY/ConPTY boundaries | Terminal/render worker | Not every VT sequence or process branch |
| Rendering/fonts/platform | Grid pipeline, font APIs, Vulkan/Metal implementations and ownership, window/UI targets | Terminal/render worker + coordinator | Full submission/shader/rasterizer audit not performed |
| Built-in modules | Markdown parsing→layout→drawing; Kanban storage/navigation/host; tests and targets | Coordinator | Detailed layout/render algorithms sampled |
| MegaCity | Scanner→semantic controller→snapshot, runtime lifecycle, backend interfaces, product tests | MegaCity/SatView worker + coordinator | Mesh/layout/routing internals sampled |
| SatView | Catalog parsing, service/worker handoff, runtime and backend boundaries | MegaCity/SatView worker + coordinator | Orbital algorithms, UI and asset tools sampled |
| ScoreView | Adapter→runtime/controllers, device boundaries, targets, fixtures, standalone wiring | Products/SDK worker | Learning/DSP/SVG internals sampled |
| PCBView | Board loading→routing/runtime, core/canvas dependencies, selection tests | Products/SDK worker | Detailed routing/render primitives sampled |
| Rezonality | Project/runtime boundaries, audio lifecycle, native adapters, tests/build/guidance | Products/SDK worker + coordinator | Compiler and GPU internals sampled |
| SDK/plugins/support | ABI, loader/staging, adapter shell, ImGui support, spinning-triangle paths | Products/SDK and terminal/render workers | Some backend/resource helpers and extraction scripts sampled |
| Build/tests/guidance | CMake edges, isolation targets, `do.py`, wrappers, CI, root/product guides and Kanban exclusions | Coordinator + all workers | No generated build-graph or runtime validation; no delegation failures |
