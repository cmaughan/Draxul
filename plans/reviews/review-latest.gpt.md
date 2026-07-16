# Draxul Repository Review

**Review date:** 2026-07-15  
**Reviewed commit:** `8fba7b8ca03a` — `ScoreView: remove the Salamander soundfont fetch — just the Yamaha`  
**Repository state after review:** clean; no files changed.

## Scope and method

I enumerated and scanned the actual files on disk rather than using `scripts/export_all_code.py` or another combined source artifact.

| Directory | Files | Text/source lines |
|---|---:|---:|
| `app/` | 45 | 14,297 |
| `libs/` | 205 | 38,577 |
| `shaders/` | 76 | 7,687 |
| `tests/` | 235 | 48,470 |
| `scripts/` | 31 | 4,982 |
| `plans/` | 68 | 20,903 |
| **Total** | **660** | **134,916** |

I also inspected the optional-module build boundaries and the latest ScoreView implementation because they materially affect `app/`, tests, host registration, and module separation. The full module trees were not part of the user-specified exhaustive directory set.

Backlog reconciliation used the live tracker, not the obsolete `plans/work-items*` paths:

- `kanban/pending`: 50 items
- `kanban/done`: 450 items
- `kanban/ice-box`: 76 items

This was a static, review-only pass. I did not build the application or execute tests because that would create or update build and test artifacts. Final verification confirmed the worktree remained clean.

## Executive assessment

Draxul has a strong lower-level foundation: real library boundaries, private renderer backends, extensive pure logic, broad tests, platform CI, and a sensible provider boundary for optional products.

The pressure point is now integration. Product growth is landing faster than the central orchestration, host contracts, tests, and documentation can absorb it. `App`, `ChromeHost`, `ScoreHost`, configuration I/O, session attachment, and cross-backend shaders are becoming high-conflict files. Several items marked complete have also regressed, which means the repository’s completion records are not currently reliable enough for multiple agents to trust without rechecking the tree.

The most urgent newly observed defect is a race in ScoreView’s asynchronous microphone opener. The most serious known-but-unresolved problems remain shell-based networking, non-atomic session persistence, renderer/shader parity gaps, and several lifecycle races already represented in `kanban/pending`.

## Current architecture

The effective architecture is better than the repository guide describes:

| Layer | Current role | Assessment |
|---|---|---|
| `draxul-types` | Shared types, logging, BMP, perf collection, runtime paths | Useful foundation, but no longer “header-only” |
| `draxul-window` | SDL window and platform event integration | Appropriate boundary; callback lifetime remains sensitive |
| `draxul-renderer` | Grid renderer plus frame/pass abstractions and backend capabilities | Cleaner than the documented obsolete hierarchy |
| `draxul-font` | Font resolution, shaping, glyph caches, rich text, text atlases | Strong reusable subsystem |
| `draxul-grid` | Cell model, highlights, dirtiness | Well isolated and heavily tested |
| `draxul-nvim` | Process, RPC, redraw and input translation | Good separation, though shutdown remains risky |
| `draxul-host` | Terminal model, host contracts and provider registry | Growing into a capability/configuration hub |
| `draxul-runtime-support` | Rendering pipeline, sessions, printing, system monitor, workers | Becoming a grab-bag integration layer |
| `app/` | Workspaces, lifecycle, actions, overlays, sessions, rendering, printing | Far thicker than “orchestration only” |
| `modules/` | Product-specific hosts and lower libraries | Good conceptually; newest host classes are becoming monolithic |

The actual renderer is `IBaseRenderer -> IGridRenderer`, with side capabilities discovered by `RendererBundle`; the actual host interface is a flat `IHost`. This contradicts the `I3DRenderer`, `I3DHost`, and `IGridHost` hierarchy still documented in [AGENTS.md](/Users/cmaughan/dev/Draxul/AGENTS.md:62).

---

# Findings

## 1. High: ScoreView microphone opening has a real lifetime race

**Status:** New; I found no matching pending, done, or ice-box item.

The asynchronous opener uses `Shared::abandoned` as a two-party ownership handshake:

- The opener exchanges `abandoned` to `true` before publishing `stream`.
- The destructor also exchanges it and, if it reads `true`, assumes the opener has finished and reads `stream`.

See [mic_player_input.cpp](/Users/cmaughan/dev/Draxul/modules/score/draxul-scoreview/src/mic_player_input.cpp:32) and [mic_player_input.h](/Users/cmaughan/dev/Draxul/modules/score/draxul-scoreview/include/draxul/scoreview/mic_player_input.h:61).

A valid interleaving is:

1. Opener creates an SDL stream.
2. Opener executes `abandoned.exchange(true)`.
3. Destructor executes its exchange and sees `true`.
4. Destructor reads `stream` before the opener has assigned it and finds `nullptr`.
5. Opener resumes the stream and publishes it after the owner has gone away.

The unsynchronised `stream` read/write is a C++ data race. It can also leak an active recording stream and let SDL audio work continue during application teardown.

The frontend has no device-lifecycle tests; current MIDI tests use the device-free `feed()` seam. The old TSan CI job recorded as complete in [WI 134](</Users/cmaughan/dev/Draxul/kanban/done/134 tsan-validation-and-ci-wiring -feature.md:54>) is no longer present in the current two-job workflow.

**Recommendation:** replace the Boolean handshake with an explicit state machine protected by a mutex, or atomically publish/take sole ownership of the stream. Inject permission and stream-opening functions so the race can be forced deterministically in a TSan test.

## 2. High: Windows process shutdown again blocks the main thread

**Status:** Reopen completed item `done/07`; do not create a duplicate card.

[NvimProcess::shutdown()](/Users/cmaughan/dev/Draxul/libs/draxul-nvim/src/nvim_process.cpp:203) waits for up to two seconds on the calling thread. [ConPtyProcess::shutdown()](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/conpty_process.cpp:442) joins its reader and can also wait two seconds after terminating the shell.

That directly conflicts with:

- The repository’s non-blocking shutdown rule in [AGENTS.md](/Users/cmaughan/dev/Draxul/AGENTS.md:135).
- The completion note claiming these waits were moved off the main thread in [done/07](</Users/cmaughan/dev/Draxul/kanban/done/07 shutdown-blocking-wait -bug.md:73>).

Closing a pane or quitting can therefore freeze the window for seconds if a child is slow or stuck.

**Recommendation:** use an application-owned process reaper or a polled shutdown state machine. Avoid detached cleanup threads without an owning lifetime, but do not synchronously wait in the UI path.

## 3. High: shell-based networking remains both an injection and shutdown hazard

**Status:** Correctly tracked by `pending/00`; do not duplicate it.

[weather_service.cpp](/Users/cmaughan/dev/Draxul/app/weather_service.cpp:192) manually encodes only a few URL characters, places the result in a quoted shell command, and executes it through `popen`. Quotes, backticks, dollar expansion, backslashes, and platform shell rules remain unsafe. `stop()` joins the worker while `curl` may remain blocked for its configured timeout.

The parser also:

- Accepts partial numeric coordinate strings.
- Does not validate latitude, longitude, or finiteness.
- Parses JSON with substring searches.
- Uses `std::tolower` on potentially signed UTF-8 bytes.

The existing [safe network transport card](</Users/cmaughan/dev/Draxul/kanban/pending/00 network-shell-transport -bug.md:1>) has the correct scope: one cancellable native transport used by weather and SatView, proper URL encoding, response limits, and real JSON parsing.

## 4. High: session persistence can destroy the last valid state

**Status:** Tracked by pending items 02 and 17.

Both topology and runtime metadata open their final files directly with `std::ios::trunc`:

- [save_session_state()](/Users/cmaughan/dev/Draxul/app/session_state.cpp:467)
- [save_session_runtime_metadata()](/Users/cmaughan/dev/Draxul/app/session_state.cpp:556)

A crash, disk-full condition, short write, antivirus interference, or power loss after truncation leaves no valid prior copy.

The feature’s complexity is spread across:

- [app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp:2289)
- [main.cpp](/Users/cmaughan/dev/Draxul/app/main.cpp:480)
- [session_attach.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-runtime-support/src/session_attach.cpp:1)
- [session_state.cpp](/Users/cmaughan/dev/Draxul/app/session_state.cpp:467)

This is a poor multi-agent boundary: CLI policy, IPC, persistence, ownership, GUI actions, and workspace restoration all intersect.

The planned atomic-write and rollback work is justified and should precede more session features.

## 5. Medium-high: pane printing is incomplete as a product and as a state machine

**Status:** New; no matching tracker item found.

Printing is documented as macOS-only in [features.md](/Users/cmaughan/dev/Draxul/docs/features.md:129), but `print_pane` is registered as a general GUI action and performs a full-frame capture on Windows before the lower layer reports “not supported” in [pane_print.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-runtime-support/src/pane_print.cpp:130).

Other issues:

- `App` owns the printing state machine directly in [app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp:1351).
- A capture that never arrives leaves `print_capture_pending_` active and keeps requesting frames without a timeout.
- Temporary PDF names have only one-second resolution and can collide.
- PDFs are not removed after print or cancellation.
- Output is a raster screenshot scaled onto A4, even for vector-capable ScoreView content.
- Tests cover crop, whitening, and macOS PDF composition, but deliberately omit the dialog and do not test the `App` capture flow.

This feature needs either a Windows backend or capability-aware action registration. The capture workflow belongs in a small controller rather than `App`.

## 6. Medium-high: recent ScoreView work has good lower layers but another god host

The Score module has an excellent conceptual split:

- Pure notation import.
- GPU-free flow, analysis, listener, model and engraving logic.
- A separate NanoVG/SDL host target.

However, [score_host.cpp](/Users/cmaughan/dev/Draxul/modules/score/draxul-scoreview/src/score_host.cpp:179) is already 2,778 lines. It owns loading, engraving, stream composition, progress, audio, MIDI, microphone input, transport, waterfall, keyboard, ImGui, printing, draw callbacks and input.

`WindowEngraver::cancel()` is also a synchronous wait in [window_engraver.cpp](/Users/cmaughan/dev/Draxul/modules/score/draxul-scoreview/src/window_engraver.cpp:77). `ScoreHost::rebuild_window()` calls it on the main thread before doing another synchronous engraving in [score_host.cpp](/Users/cmaughan/dev/Draxul/modules/score/draxul-scoreview/src/score_host.cpp:783). User actions such as restart, mode changes, and inspector changes can therefore wait for an in-flight roughly 100 ms engrave and then perform another one.

The unit target links `draxul-scoreview`, but not `draxul-scoreview-host`, in [tests/CMakeLists.txt](/Users/cmaughan/dev/Draxul/tests/CMakeLists.txt:54). Lower algorithms are well tested; the actual host, microphone frontend, NanoVG integration and shutdown are not.

Suggested internal boundaries:

- `ScoreSessionController`: load, persistence and analysis.
- `ScoreStreamController`: rolling windows, worker generations and carry state.
- `ScoreAudioController`: capture, MIDI, synth and device switching.
- `ScoreViewModel`: inspector/status data.
- `ScorePresentation`: NanoVG draw and layout.

## 7. Medium: `App` and `ChromeHost` remain central merge-conflict magnets

The largest production hotspots include:

- [app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp:1): 2,975 lines.
- [chrome_host.cpp](/Users/cmaughan/dev/Draxul/app/chrome_host.cpp:356): 1,543 lines, with a 643-line `draw()`.
- [main.cpp](/Users/cmaughan/dev/Draxul/app/main.cpp:480): 955 lines.
- [input_dispatcher.cpp](/Users/cmaughan/dev/Draxul/app/input_dispatcher.cpp:223): 922 lines.
- [host_manager.cpp](/Users/cmaughan/dev/Draxul/app/host_manager.cpp:1): 842 lines.

`App` owns initialization rollback, window and renderer lifecycle, font propagation, actions, workspaces, overlays, weather, printing, screenshots, sessions, attach/detach, config reload and shutdown. `wire_gui_actions()` alone is roughly 300 lines.

The static fallback returned by [active_host_manager()](/Users/cmaughan/dev/Draxul/app/app.cpp:2881) is particularly dangerous: invalid workspace state silently turns into mutations of a process-wide dummy object. This is already tracked by pending item 13.

Pending items 22–25 correctly target workspace/session/chrome/main decomposition. They should be implemented before adding more app-wide features.

## 8. Medium: the host interface is becoming a capability and configuration grab-bag

[IHost](/Users/cmaughan/dev/Draxul/libs/draxul-host/include/draxul/host.h:160) is flat and increasingly broad. It includes:

- Full lifecycle and frame scheduling.
- Every input type.
- Action dispatch.
- Neovim and Markdown type queries.
- Status, cwd and exit information.
- Print hints.
- ImGui attachment and font injection.
- Scroll offsets.

`IHostCallbacks` also exposes product-specific `dispatch_to_nvim_host()` and `open_markdown_source()` operations. `HostLaunchOptions` and `HostReloadConfig` combine terminal, Markdown, selection, scrolling and generic presentation settings in the same header.

[HostKind](/Users/cmaughan/dev/Draxul/libs/draxul-types/include/draxul/host_kind.h:12) hard-codes every optional product in a bottom-level library, while [HostProviderRegistry](/Users/cmaughan/dev/Draxul/libs/draxul-host/include/draxul/host_registry.h:28) stores only a kind and factory. This is why CLI help, palettes, session restore and optional build availability can disagree.

The provider registry itself is a good boundary. The next step should be adding provider metadata and narrower optional capability interfaces, as already covered by pending items 12 and 31.

## 9. Medium: `runtime-support` and `app-support` no longer express coherent ownership

[draxul-runtime-support](/Users/cmaughan/dev/Draxul/libs/draxul-runtime-support/CMakeLists.txt:1) contains:

- Cursor blinking.
- Grid rendering.
- Pane printing.
- Session IPC.
- System monitoring.
- UI worker infrastructure.

It publicly links config, font, grid, nvim, renderer and window. That makes it a broad dependency funnel rather than a cohesive reusable library.

`draxul-app-support` is now only an interface library that forwards config, runtime support and render-test. More importantly, the production `draxul-app` target still unconditionally links `draxul-render-test` in [CMakeLists.txt](/Users/cmaughan/dev/Draxul/CMakeLists.txt:278), and the executable always registers the NanoVG demo provider in [main.cpp](/Users/cmaughan/dev/Draxul/app/main.cpp:522).

This regresses the completed render-test extraction item. It should be reopened rather than duplicated.

Natural replacement libraries would be:

- `draxul-session`
- `draxul-system-services`
- `draxul-pane-capture`
- `draxul-grid-presentation`

## 10. Medium: manual shader parity is past a comfortable scale

The Vulkan path has many small GLSL files, while [satview_scene.metal](/Users/cmaughan/dev/Draxul/shaders/satview_scene.metal:1) is 1,835 lines containing the corresponding Metal entry points. Buffer indices, texture indices, vertex records, push constants and draw ordering are duplicated manually across:

- C++/Objective-C++ renderer code.
- GLSL vertex and fragment files.
- One large Metal shader.
- CMake shader dependency lists.
- Source-reading tests.

This is already represented by pending shader/resource items 04–06, 19, 26 and 28. The important architectural direction is shared ABI declarations or generated validation metadata, not trying to merge the two backend implementations.

The stale root CMake comments about removed `I3DPassProvider` APIs in [CMakeLists.txt](/Users/cmaughan/dev/Draxul/CMakeLists.txt:28) illustrate how quickly adjacent documentation drifts.

## 11. Medium: the test suite is broad but structurally monolithic

The suite has **1,595 Catch2 cases** across 163 C++/Objective-C++ files. It is one of the strongest parts of the repository.

Recent improvements are real:

- Four Catch2 shards are registered in [tests/CMakeLists.txt](/Users/cmaughan/dev/Draxul/tests/CMakeLists.txt:96).
- `tests/do_py_tests.py` is now registered with CTest.
- CI runs automatically on pushes and pull requests on Windows and macOS in [build.yml](/Users/cmaughan/dev/Draxul/.github/workflows/build.yml:3).
- Slow fuzz cases are enabled in both CI jobs.
- GPU/application tests use a resource lock.

Remaining structural problems:

1. All C++ tests still compile and link as one `draxul-tests` executable.
2. Tests include optional-module private `src/` headers through explicit include paths in [tests/CMakeLists.txt](/Users/cmaughan/dev/Draxul/tests/CMakeLists.txt:31).
3. The entire `session_attach_tests.cpp` file is excluded on Apple due to a Catch2 formatting issue at [tests/CMakeLists.txt](/Users/cmaughan/dev/Draxul/tests/CMakeLists.txt:77).
4. There are 131 conditional `SKIP()` calls, many around a bundled font that should be a controlled test dependency.
5. ScoreView host and audio frontends are not linked into the test executable.
6. The old TSan CI job is absent.

Pending item 35 already covers modular test targets, and pending item 15 covers Apple session-attach coverage.

## 12. Medium: render-test discovery has four competing sources of truth

Current files disagree:

- Ten scenario TOMLs exist.
- Only five scenario families have platform references.
- `do.py renderall` runs five scenarios.
- CTest lists five different names, including missing `ligatures-view`, but omits NanoVG.
- `docs/features.md` lists `wide-char-scroll` and the absent ligature scenario.
- CMake silently skips missing scenario/reference pairs.

See [CMakeLists.txt](/Users/cmaughan/dev/Draxul/CMakeLists.txt:501) and [features.md](/Users/cmaughan/dev/Draxul/docs/features.md:453).

Consequently, CI can be green while newly added scenario files are never compared. Pending items 03 and 16 correctly cover a manifest plus a testable comparison core.

## 13. Medium: the new shared text atlas needs bounded failure semantics

The extraction into `draxul-font` is a good architectural move, but [text_atlas_builder.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-font/src/text_atlas_builder.cpp:37) currently:

- Implements a partial grapheme-clustering algorithm.
- Does not join regional-indicator flag pairs.
- Stops silently on invalid UTF-8.
- Estimates width as one fixed cell per cluster.
- Can allocate an 8192×8192 RGBA atlas—256 MiB.
- Silently drops labels that still do not pack at the maximum size.
- Silently keeps the first duplicate key.

The four tests in [text_atlas_builder_tests.cpp](/Users/cmaughan/dev/Draxul/tests/text_atlas_builder_tests.cpp:43) cover determinism, one accented string, duplicate keys, alignment and a single oversized request. The active SatView plan explicitly asked for packing, elision, Unicode and overflow tests, so this is unfinished active-plan acceptance rather than a new backlog concept.

The builder should return an explicit result containing dropped keys and failure reasons, and enforce a configurable byte/count budget.

## 14. Medium-low: performance instrumentation adds overhead even when disabled

`PERF_MEASURE()` is used throughout small and hot functions. Every scope constructs a timer and calls `RuntimePerfCollector::enabled()`, which locks a global mutex in [perf_timing.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-types/src/perf_timing.cpp:61). The timestamp is also captured before the enabled check in [ScopedPerfMeasure](/Users/cmaughan/dev/Draxul/libs/draxul-types/src/perf_timing.cpp:371).

This can distort the functions being measured and adds unnecessary contention when diagnostics are off. Pending item 33 already owns the audit. A lock-free atomic enabled flag should be the first simplification.

## 15. Medium: documentation and planning are active sources of agent error

Several sources of truth disagree with the current tree:

- [AGENTS.md](/Users/cmaughan/dev/Draxul/AGENTS.md:64) describes removed renderer and host hierarchies and calls `draxul-types` header-only.
- [AGENTS.md](/Users/cmaughan/dev/Draxul/AGENTS.md:116) still instructs agents to move cards through deleted `plans/work-items*` directories.
- [plans/README.md](/Users/cmaughan/dev/Draxul/plans/README.md:1) repeats those paths.
- [sync_project_board.py](/Users/cmaughan/dev/Draxul/scripts/sync_project_board.py:28) uses the deleted directories and fetches only the first 100 project items.
- [docs/features.md](/Users/cmaughan/dev/Draxul/docs/features.md:20) has an 18,663-character ScoreView table row. SatView has similarly oversized rows. These are product notebooks embedded in a summary table and will conflict on almost every feature branch.
- `plans/` contains 60 Markdown plans and 20,668 lines, with completed implementation plans mixed beside active designs and inconsistent status markers.
- Review automation still contains broad unattended permission modes; this is already pending item 14.

The repository’s done cards for stale docs, project-board navigation and TSan/CI wiring should be reopened. Pending item 34 is the right owner for repository hygiene and the feature-document source of truth.

A better documentation shape would be:

- Short canonical host summary in `docs/features.md`.
- Product-owned detail pages such as `docs/features/scoreview.md`.
- A generated provider/config/action table.
- A plan index recording `active`, `implemented`, `superseded`, or `research`.
- Tracker checks that flag completed cards whose acceptance criteria no longer match the tree.

---

# Multi-agent maintainability

## Highest-conflict files

| File | Why it conflicts | Recommended owner boundary |
|---|---|---|
| `app/app.cpp` | Every app-wide action and controller lands here | Workspace, session, capture and service controllers |
| `app/chrome_host.cpp` | Layout, drawing, hit-testing, text and editing mixed | Layout model, NanoVG painter, text-grid presenter |
| `app/main.cpp` | CLI, process spawning, sessions and provider registration | CLI command handlers and platform launch helpers |
| `app/input_dispatcher.cpp` | Keyboard, chords, mouse, scrolling and overlays | Keep one owner until the event-routing split is designed |
| `app/session_state.cpp` | Schema, validation, paths and persistence | Serializer plus atomic storage adapter |
| `libs/draxul-runtime-support/src/session_attach.cpp` | Windows/POSIX protocol in one large unit | Neutral protocol plus platform transports |
| `libs/draxul-config/src/app_config_io.cpp` | Defaults, parsing, validation, serialization | Declarative schema work in pending 21 |
| `shaders/satview_scene.metal` | Every SatView shader change touches one file | Split by scene family after ABI validation exists |
| `modules/score/.../score_host.cpp` | Every ScoreView subsystem meets here | Session, stream, audio and presentation controllers |

Agents should not independently edit `app.cpp`, `chrome_host.cpp`, `main.cpp`, a product host, and its provider/config wiring in parallel. Assign a single integration owner and give other agents lower-library or test boundaries.

## Recommended remediation order

1. Fix the microphone lifetime race.
2. Reopen and fix non-blocking Windows shutdown.
3. Complete pending bugs 00–14, especially network transport and atomic sessions.
4. Land the pending failure-path tests 15–20.
5. Extract the app workspace/session/chrome controllers in pending 22–25.
6. Establish provider metadata and declarative configuration.
7. Split test targets and restore TSan coverage.
8. Repair steering documentation and plan status.
9. Only then add more app-wide or persistent-session features.

---

# Backlog reconciliation

## Newly identified or materially new since the saved July review

- ScoreView microphone open/destruction data race.
- No ScoreView host/audio integration test target.
- Main-thread blocking `WindowEngraver::cancel()` during user-driven rebuilds.
- Pane-print capture timeout, action availability, temporary-file and integration gaps.
- Shared text-atlas allocation/drop semantics and missing active-plan tests.
- ScoreView’s rapid growth into a 2,778-line host.
- Product feature documentation becoming unreviewable single-line records.

## Completed items that should be reopened

- `done/07`: shutdown-blocking wait.
- `done/19`: production render-test extraction.
- `done/111` and `done/12`: stale design/agent documentation.
- `done/01`: stale planning navigation and board sync.
- `done/14`: tests depending on private module source headers.
- `done/134`: TSan CI wiring.

## Previously stale findings that are now fixed

The current report does **not** repeat two findings from the saved July consensus:

- CI now runs on push and pull requests.
- `tests/do_py_tests.py` is now wired into CTest, and Catch2 sharding has been added.

## Ice-box items intentionally not re-proposed

I did not re-propose searchable scrollback, focus mode, keybinding inspector, clipboard history, config GUI, live config reload, host lifecycle state machines, renderer parity cleanup, per-monitor DPI, pane drag/reorder, right-click menus, performance HUD, command-palette MRU, agent-script deduplication, split stress, atlas exhaustion or concurrent-host shutdown. Those remain deliberately iced.

---

# Top 10 good things

1. **The lower-level library split is real.** Grid, font, nvim, window, renderer and terminal logic are meaningfully separated, not just separate folders.

2. **Optional product registration is source-isolated.** Optional modules register providers from the executable rather than being constructed in core host code.

3. **Renderer backends stay private.** App code depends on renderer contracts rather than Vulkan or Metal implementation headers.

4. **Pure logic receives serious attention.** Terminal parsing, grid mutation, scoring, notation import, projections, geometry and filtering are testable without opening a window.

5. **Test breadth is excellent.** 1,595 Catch2 cases cover terminal behavior, RPC, Unicode, config, selection, split trees, product logic and failure paths.

6. **The test workflow recently improved.** Four shards, Python test registration, GPU resource locking and slow-test CI execution are meaningful stability gains.

7. **Both primary platforms are first-class in CI.** Windows/Vulkan and macOS/Metal are built and tested on pushes and pull requests.

8. **Reusable test support is strong.** Replay fixtures, fake renderer/window/hosts, temporary-directory helpers and synthetic audio fixtures make difficult behavior reproducible.

9. **Data provenance is often handled well.** Several SatView asset builders use pinned sources, hashes, manifests and atomic replacement.

10. **The live pending queue is unusually actionable.** The 50 current cards have concrete implementation steps, tests, acceptance criteria and ownership guidance.

# Top 10 bad things

1. **The ScoreView microphone opener has an untracked data race and resource-lifetime bug.**

2. **Windows shutdown can block the UI for up to two seconds despite being marked fixed.**

3. **Weather and SatView networking still interpret strings through a command shell.**

4. **Session topology and metadata overwrite their last valid state non-atomically.**

5. **Central integration classes are growing faster than they are being decomposed.**

6. **`IHost`, host callbacks, launch options and reload configuration are becoming product-specific grab-bags.**

7. **Vulkan/Metal shader and resource contracts are duplicated manually at a scale where drift is likely.**

8. **One monolithic test executable reaches through private module boundaries and excludes an entire Apple test file.**

9. **Steering docs, completed cards and automation do not reliably describe the live tree.**

10. **New features sometimes land with knowingly partial platform or lifecycle behavior, with pane printing being the clearest recent example.**

# Best 10 quality-of-life features to add

These were cross-checked textually against `docs/features.md`, the current pending queue, done cards and ice-box; I found no equivalent tracked feature.

1. **Move a pane between workspaces.** Let users send the focused pane to an existing workspace without restarting its host.

2. **Duplicate an entire workspace.** Clone its split topology and launch descriptors, complementing the existing single-pane duplicate action.

3. **Context-aware file dropping.** Open files in Nvim/Markdown/ScoreView, but insert safely quoted paths into shell panes.

4. **Paste transformations.** Offer normal paste, single-line paste and shell-escaped paste from the palette or confirmation overlay.

5. **Selected-pane input broadcast.** Send keyboard input to an explicit group of terminal panes, with a prominent active indicator and confirmation.

6. **Split-layout presets.** Add balanced columns, rows, grid and main-plus-stack arrangements without repeated manual splitting.

7. **Export focused pane as PNG.** Reuse the cross-platform capture renderer without tying export to macOS printing.

8. **Workspace and pane lock.** Prevent accidental close, restart or replacement of important long-running panes.

9. **Command-palette argument completion.** Complete host kinds, workspace names, session names and filesystem paths for parameterised actions.

10. **Workspace/pane color labels.** Give tabs and status pills user-assigned colors or short tags for large multi-workspace sessions.

# Best 10 tests to add

1. **Microphone opener ownership race — new.** Inject a blocked opener, destroy at every handoff point, and verify exactly one stream owner and one destruction under TSan.

2. **Real Windows process shutdown latency — reopen `done/07`.** Hold fake Nvim and ConPTY children open and assert pane close returns to the event loop within one frame.

3. **Atomic session-write fault injection — pending 02/17.** Fail creation, write, flush and replacement independently and prove the old session remains loadable.

4. **macOS session-attach suite — pending 15.** Remove the whole-file exclusion and test attach, detach, rename, kill, timeout and stale-owner takeover.

5. **ScoreHost integration smoke/render/shutdown — new.** Construct the actual host target with a MusicXML fixture, run frames, switch input/modes, resize, print-hint and shut down.

6. **Render manifest/comparison contract — pending 03/16.** Assert every declared scenario has valid platform reference policy and is visible to CTest, `do.py` and documentation.

7. **Cross-backend shader ABI validation — pending 19.** Check CPU struct sizes/offsets, GLSL locations/bindings and Metal buffer/texture indices from one contract.

8. **Pane-print state machine — new.** Cover capture timeout, repeated action, focus change during capture, invalid host hint, cancellation, cleanup and unsupported-platform availability.

9. **Text-atlas Unicode and budget corpus — active plan gap.** Add flags, ZWJ emoji, combining marks, malformed UTF-8, elision boundaries, packing overflow, dropped-key reporting and allocation limits.

10. **ScoreView worker/device stress — new.** Rapidly restart/re-engrave while a worker is active, switch keyboard/MIDI/microphone repeatedly, flood MIDI events and require bounded main-thread latency and queue size.

# Worst 10 existing features by engineering risk

“Worst” here means highest risk-to-value implementation, not that the user-facing idea is undesirable.

1. **Live microphone input:** valuable, but currently contains a lifetime race and has no frontend lifecycle tests.

2. **Weather pill:** a small cosmetic feature currently carries shell-injection, fragile parsing, external-command and shutdown risk.

3. **Persistent live sessions:** high user value, but spans process spawning, IPC, persistence, CLI, workspace restoration and platform-specific ownership with weak failure atomicity.

4. **Pane printing:** macOS-only, raster-based, embedded in `App`, lacks capture timeout and cleanup, and exposes a failing action elsewhere.

5. **ScoreView rolling runner:** innovative but concentrated in a huge host with synchronous rebuild/cancellation edges and no host integration suite.

6. **Render snapshots:** useful in principle, but manifest drift and silent skips make green results weaker than they appear.

7. **Runtime configuration reload:** distributed manual field propagation makes partial application and stale service state likely.

8. **Optional host selection:** `HostKind` advertises compile-time products without registry metadata, allowing CLI/palette/session availability drift.

9. **Production NanoVG demo registration:** a test/demo host and render-test library remain coupled to the shipping application.

10. **SatView cross-backend scene pipeline:** feature-rich but maintained through very large, manually mirrored resource and shader contracts that are difficult to review safely in parallel.