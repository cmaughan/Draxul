The review is complete. Here is the full report.

---

# Draxul Codebase Review — 2026-07-15

**Scope:** Direct file-level review of `app/`, `libs/`, `modules/`, `shaders/`, `tests/`, `scripts/`, `plans/`, `kanban/`, `docs/`, and build wiring, read from disk at commit `8fba7b8` ("ScoreView: remove the Salamander soundfont fetch"). ~758 first-party source files: ~116k lines in `app/`+`libs/`+`modules/`, ~45k lines of tests, ~70 shaders, 1,578-line `do.py` orchestrator. Completed (`kanban/done`, 450 items) and iced (`kanban/ice-box`) work is excluded per instructions; where a finding is already tracked in `kanban/pending`, the item number is cited so this review reinforces rather than re-discovers.

---

## 1. Architecture: As-Built vs. As-Documented

Draxul has outgrown its own map. CLAUDE.md's "Project Structure" describes seven libraries (`types`, `window`, `renderer`, `font`, `grid`, `nvim`, `app-support`) and a single `app/`. The tree on disk actually contains **fourteen libraries** (`draxul-config`, `draxul-gui`, `draxul-host`, `draxul-nanovg`, `draxul-render-test`, `draxul-runtime-support`, `draxul-ui` are all undocumented in the structure diagram) plus an entire `modules/` hierarchy (kanban, markdown, megacity/bioview, satview, score) that the diagram doesn't mention at all. Concrete staleness examples:

- CLAUDE.md calls `draxul-types` "header-only"; it compiles `bmp.cpp`, `log.cpp`, `perf_timing.cpp`, `runtime_path.cpp`.
- `docs/module-map.md` (last touched April 2026) still points to `plans/work-items/`, which has been replaced by `kanban/pending|done|ice-box`.
- The dependency graph in CLAUDE.md ends at "app-support → app"; in reality `draxul-runtime-support` publicly links `draxul-nvim`, and `draxul-host` consumes `draxul-nvim` headers **without declaring the dependency** (it works only because `draxul-runtime-support` propagates it transitively — see §3).

The as-built layering is genuinely good — hosts are pluggable behind `IHost`, render passes behind `IRenderPass`, backends behind `IBaseRenderer`/`IFrameContext`, and the app layer is dependency-injected via `Deps` structs everywhere (`HostManager::Deps`, `ChromeHost::Deps`, `InputDispatcher::Deps`, `AppDeps` factories). But the *documented* architecture is roughly a year of development behind the actual one, and for a codebase explicitly built for multiple agents, the primary onboarding document actively misleads.

## 2. Module Separation — What Works

- **The host abstraction carries the whole product.** Ten host kinds (nvim, four shells, markdown, kanban, megacity, bioview, satview, score) run through one lifecycle: `initialize/pump/draw/dispatch_action/shutdown` with default no-op virtuals so simple hosts stay small. `GridHostBase` → `TerminalHostBase` → concrete shells is a clean template-method stack; the terminal layer (vt_parser, sgr, key encoder, scrollback, alt-screen, selection, mouse reporter) is decomposed into single-purpose files with matching test files for nearly every one.
- **Modules are real CMake boundaries.** `modules/megacity` is split into geometry / code-semantics / treesitter / codeviz-scene / codeviz-renderer / host libraries; `modules/score` splits notation import from the scoreview runtime. Feature gates (`DRAXUL_ENABLE_MEGACITY/SATVIEW/SCOREVIEW`) exist and the test glob filters respect them.
- **ScoreView (the newest, fastest-moving code) is better-factored than the older 3D hosts:** `FlowController`, `StreamComposer`, `PlayerModel`, `WindowEngraver` (a properly mutex/condvar-disciplined background engrave worker), `SourceSlicer`, `NoteListener` are each independently unit-tested (12 scoreview test files). The lesson of megacity/satview was visibly learned.
- **Shared shader constants** (`decoration_constants_shared.h`, `quad_offsets_shared.h`) keep GLSL/MSL/C++ in agreement — the right pattern for a dual-backend codebase.

## 3. Module Separation — What Doesn't

1. **Dual-backend render passes are written twice, by hand, per module.** `codeviz_render_vk.cpp` (4,994 lines) vs `codeviz_render.mm` (2,173); `satview_render_vk.cpp` (2,928) vs `satview_render.mm` (1,720); `markdown_render_pass_vk.cpp` (1,023) vs `_metal.mm` (559); NanoVG backends twice more. Both sides define parallel `Buffer`/`Image`/`PushConstants`/`FrameResources` structs and the same upload/transition/pipeline boilerplate. Every scene-layer feature costs two implementations and two review passes, and Vulkan/Metal drift is caught only by render snapshots. This is the single largest structural tax on the repo. (Tracked: pending #28 *shared-gpu-resource-helpers*, #25 iced parity-cleanup; the underlying mid-level GPU abstraction still doesn't exist.)
2. **God hosts.** `satview_host.cpp` is 5,284 lines and owns simulation control, six ImGui dock panels, camera state, config persistence, catalog wiring, and per-frame draw-stream assembly (`draw()` alone spans ~490 lines, lines 1257–1745). `megacity_host.cpp` (2,992) and `score_host.cpp` (2,778, with a 420-line `render_debug_ui`) have the same shape. Two agents cannot safely work on "satview UI" and "satview propagation" concurrently — they will collide in one file. (Tracked: pending #26, #27.)
3. **SDL leaks through the "abstract" event layer.** `KeyEvent` carries raw SDL keycodes as `int`, so 23 files outside `draxul-window` — including hosts in every module, `draxul-config`'s keybinding parser, and `libs/draxul-nvim/src/input.cpp` — include `<SDL3/SDL.h>` and switch on `SDLK_*`. The RPC library linking SDL3 is the starkest symptom. The `IWindow` abstraction exists but its vocabulary was never made platform-neutral, so "swap the window layer" or "headless input replay" is much harder than the class diagram suggests.
4. **Undeclared/inverted dependencies.** `draxul-host/src/nvim_host.h` includes `<draxul/nvim.h>` but `draxul-host/CMakeLists.txt` never links `draxul-nvim`; it compiles because `draxul-runtime-support` (a *generic* helpers library: cursor blinker, pane print, system monitor) publicly links `draxul-nvim`. Generic support depending on the Neovim RPC stack is a layering inversion, and the undeclared edge will break the first time someone reorders links or consumes `draxul-host` standalone. (Adjacent: pending #31 *foundation-dependency-cleanup*.)
5. **Capability queries hardcode host types into the base interface.** `IHost::is_nvim_host()` / `is_markdown_host()` are per-type booleans on the root interface; the next routing need (score? kanban?) adds another. A single `supports(capability)` query or a typed-interface query would stop the interface from growing per host.
6. **Two UI libraries with near-identical names.** `draxul-gui` (non-ImGui overlay renderers: palette, toast, tooltip) and `draxul-ui` (ImGui panels + SDL-ImGui input). The split is defensible; the naming guarantees agents put code in the wrong one. (Adjacent: pending #29 *gui-ui-contracts*.)
7. **App is still a 2,975-line orchestrator** mixing lifecycle, session persistence/attach, workspace management, the print pipeline, and a ~300-line `wire_gui_actions`. It's well-commented and delegates a lot, but sessions alone (`save/load/snapshot/persist/checkpoint/attach/detach/kill/rename`) are ~800 lines that belong in a controller. (Tracked: pending #22, #24.)

## 4. Code Smells & Maintainability Notes

- **Hygiene at the root** (tracked: pending #34, confirmed still present): `key.txt` is a *tracked* file whose name screams "credential" but whose content is a DPI debug log; `megacity-linux-drivers-mesh.bmp` sits tracked at the root; `debug.log`, `draxul.log`, `default.profraw`, `tmp/` float in the working tree; `FEATURES.md` (259-line user guide) and `docs/features.md` (473-line canonical inventory) coexist with overlapping content.
- **Four divergent agent-instruction files** — `CLAUDE.md` (275 lines), `AGENTS.md` (173), `GEMINI.md` (190), `learnings_agents.md` (141) — all differ. For a multi-agent workflow this is triple-maintenance and guarantees the three agent families operate under different rules. One canonical file with thin per-agent pointers would remove a whole class of drift.
- **`docs/features.md` is becoming write-only.** The ScoreView table row is a single Markdown cell of several thousand words. The doc is the designated dedup gate for new work items ("check features.md before creating work items"), but nobody can diff or skim a 4,000-word table cell. It also drifts: `weather_location` (a real config key in `app_config_types.h:96`) is undocumented — only its pill color appears.
- **WeatherService** (`app/weather_service.cpp`) shells out to `curl` via `popen`, hand-parses JSON with no parser, spawns a background thread, and has zero tests and no injection seam. It's the only subsystem in the app layer with this shape.
- **Detached threads** at `nvim_process.cpp:563` (SIGKILL helper), `unix_pty_process.cpp:334`, `conpty_process.cpp:372`, and `mic_player_input.cpp:76` (captures shared state `s` while the audio device opens asynchronously). Each is individually defensible, but detached threads plus process shutdown is where the next hard-to-reproduce crash lives; the mic one interacts with a TCC consent dialog of arbitrary duration.
- **`do.py` is a 1,578-line monolith** including hand-rolled TOML line-editing (`_merge_key_value`, `_table_end`), MSVC env capture, deploy staging, and review orchestration. It does have real unit tests (`tests/do_py_tests.py`) — credit where due — but it's the second-largest single-file surface an agent must reason about after the god hosts.
- **Positives worth naming:** only 3 TODO/FIXME comments in ~116k lines; essentially no raw `new`/`delete`; `Result<T,E>` used at real failure boundaries; `ThreadCheck` asserts on grid/pipeline/Metal renderer; comments consistently explain *constraints* ("WI 24: returns a Result so callers can observe failure") rather than narrating code. Comment discipline here is genuinely better than most professional codebases.

## 5. Multi-Agent Workability

The repo is unusually well-instrumented for agents (kanban lanes, consensus-review prompts, `plans/` manifestos, generated deps/UML docs, `learnings_agents.md`) but four things actively hurt parallel agent work:

1. **Stale primary map** (CLAUDE.md structure/dep-graph, module-map.md) — agents plan against a fiction, then waste turns rediscovering `modules/` and `draxul-host`.
2. **God-file collision zones** — `satview_host.cpp`, `megacity_host.cpp`, `score_host.cpp`, `app.cpp`: any two tasks touching the same host serialize on one file; merge conflicts are near-guaranteed.
3. **One monolithic test binary** — every `*_tests.cpp` links into `draxul-tests` with every module; a scoreview-only change pays satview/megacity link time, and a broken satview test blocks scoreview iteration. (Tracked: pending #35 *modular-test-targets* — this should be prioritized; it is the highest-leverage enabler for agent parallelism.)
4. **Doc duplication with no single source of truth** — FEATURES.md vs docs/features.md, four agent files: agents cite whichever copy they found first.

## 6. Testing Assessment

**Strengths:** ~1,600 TEST_CASE/SECTIONs across ~150 files; fuzz tests (`mpack_fuzz`, `vt_parser_fuzz`, `vtparser_overflow`); replay fixtures for redraw bugs; a rich fake library (`fake_renderer`, `fake_rpc_channel`, `fake_window`, `synthetic_piano`, `pty_capture_fixture`); sharded ctest (4 shards, labeled, 300s timeouts); ASan/TSan/coverage presets with a suppressions file; CI on Windows + macOS running unit *and* render snapshot tests with `DRAXUL_RUN_SLOW_TESTS=1`; `draxul-rpc-fake` dependency made explicit; even `do.py` has unit tests. This is a top-decile testing culture for a solo/agent-driven project.

**Holes (excluding iced test items):**

- `session_attach_tests.cpp` is **excluded on APPLE** (tests/CMakeLists.txt:80–84, a Catch2/libc++ workaround) — the session attach/detach machinery has *zero* test coverage on the primary development platform (pending #15 tracks this; the Catch2 pin should be revisited since newer Catch2 fixed the `__int128` StringMaker issue).
- **No host-level tests for the three biggest hosts.** ScoreView's subsystems are tested, but `ScoreHost` orchestration (async engrave install/carry, mode toggles, input dropdown switching) is not; same for SatViewHost (pending #18) and the render-side of MegaCityHost.
- **Renderer backends** have unit tests only for helpers (`vk_resource_helpers`, `renderer_state`); swapchain loss, resize-during-frame, and atlas-upload abort paths (pending bugs #04, #05) rely entirely on end-to-end snapshots.
- **WeatherService**: untestable as written (no seam around `run_curl`).
- **Audio voices**: metronome has WAV-verified tests; `SoundfontSynth` (sample-exact strike/release, lazy .sf2 load, click-free instrument switching) has none.
- **MIDI failure paths**: commit `28a5801` fixed an RtMidi `throw()`-terminate crash; I found no regression test pinning that behavior.
- **`.mxl` robustness**: compressed MusicXML is a zip; truncated/hostile archives should fail gracefully — no negative-path tests visible in `notation_importer_tests`.

---

## Top 10 Good Things

1. **Testing culture** — 45k lines of tests, fuzzers, replay fixtures, fakes, shards, sanitizer presets, and CI that actually runs render snapshots on both platforms.
2. **Dependency injection everywhere in the app layer** — `Deps` structs, `AppDeps` factories, `IInputRouter`; almost every seam a test needs already exists.
3. **The host abstraction** — ten wildly different hosts (terminal → music tutor → satellite globe) share one small, default-no-op lifecycle interface without leaking into each other.
4. **Terminal emulation layer decomposition** — vt_parser/sgr/key-encoder/scrollback/selection/alt-screen each single-purpose, each with a matching test file.
5. **ScoreView's internal factoring** — FlowController/StreamComposer/PlayerModel/WindowEngraver/SourceSlicer with per-unit tests; the newest code shows architectural learning from the older 3D hosts.
6. **Process/work-item discipline** — kanban lanes with 450 completed items, consensus-review prompts, manifestos; rare even in funded teams.
7. **Comment quality** — comments state constraints and provenance ("WI 128", "gap-fill only, so a head measure that re-declares a clef keeps its own"), not narration.
8. **Cross-platform parity engineering** — shared shader-constant headers, backend-neutral scene streams, bless-able render references per scenario.
9. **Modern C++ hygiene** — `Result<T,E>`, spans, RAII, ~zero raw new/delete, 3 TODOs total, `ThreadCheck` asserts on thread-confined state.
10. **Build ergonomics** — ccache auto-wire, presets for every config, `CONFIGURE_DEPENDS` test discovery, test PCH, one `do.py` entry point with its own tests.

## Top 10 Bad Things

1. **Hand-written dual-backend render passes** — ~14k lines of Vulkan/Metal near-duplication across codeviz/satview/markdown/nanovg; every feature is written twice (pending #28 understates the urgency).
2. **God hosts** — satview_host 5,284 / megacity_host 2,992 / score_host 2,778 lines; the primary collision zones for parallel agents (pending #26/#27).
3. **Stale architecture docs** — CLAUDE.md's structure diagram omits half the libraries and all of `modules/`; module-map.md points at a moved work-item folder; "header-only" claims are false.
4. **SDL keycode leakage** — raw SDL constants in `KeyEvent` drag `<SDL3/SDL.h>` into 23 files across all layers, including the config parser and the nvim RPC library.
5. **Undeclared/inverted deps** — `draxul-host` uses `draxul-nvim` without linking it; generic `draxul-runtime-support` publicly links the nvim RPC stack.
6. **Monolithic test target** — one binary links every module; per-module iteration and agent parallelism pay the full link tax (pending #35).
7. **Root-level hygiene** — tracked `key.txt` log dump, root BMP, stray logs/profraw, duplicate FEATURES.md, four divergent agent-instruction files (pending #34 covers part).
8. **`docs/features.md` format collapse** — thousand-word table cells make the canonical inventory undiffable and unskimmable; keys like `weather_location` already missing.
9. **App.cpp scope** — lifecycle + sessions + workspaces + printing + action wiring in 2,975 lines (pending #22/#24).
10. **macOS test blind spot** — session attach tests compiled out on the platform where development actually happens.

## 10 Best Quality-of-Life Features to Add

*(checked against docs/features.md, kanban/pending, and kanban/ice-box to avoid duplicates)*

1. **Piece library for ScoreView** — launching `--host score` without `--source` shows a placeholder page; instead list recent pieces with progress summaries (tempo attained, mastery %) mined from the existing `scoreview/progress/*.json`.
2. **Practice loop markers** — let the player select a bar range (click-drag on the sheet or `[`/`]`-style marks) and loop it with the existing windowed engraver; today only the composer decides what repeats.
3. **Metronome count-in** — one or two bars of ticks before the transport starts rolling (and after `r` rewind), so the player's hands are ready at beat one.
4. **Manual hand-mute toggle in Roll mode** — the simplification ladder already isolates a weak hand automatically; expose left/right-hand mute as a direct inspector/keyboard toggle for deliberate practice.
5. **End-of-session recap** — on exit or `Esc`, a one-screen summary: bars promoted, tempo curve, worst chords, streak — the data all exists in PlayerModel/progress JSON.
6. **Type-aware file-drop routing** — file drop currently dispatches `open_file:` to the focused host; route `.md` to a Markdown pane, `.musicxml/.mxl` to a Score pane, directories to a shell pane at that cwd.
7. **"Split with host…" palette action** — splits currently always spawn the platform shell; a palette action that fuzzy-picks the host kind (nvim/markdown/score/...) for the new pane removes a restart round-trip.
8. **Audio output device picker** — ScoreView mixes metronome/audition/soundfont into the default SDL device; a device combo (plus hot-unplug fallback) in the Audio section.
9. **MIDI auto-reconnect** — hot-unplugged MIDI input currently requires reopening the combo; detect disconnect, toast it, and re-attach when the port reappears.
10. **OS theme follow** — chrome colors are fully configurable but static; an opt-in `follow_system_appearance` that swaps between two named chrome palettes on macOS/Windows dark-mode changes.

## 10 Best Tests to Add for Stability

*(excluding iced test items and citing pending where they align)*

1. **ScoreHost orchestration test** — headless host with fake renderer: async window advance under a slow `WindowEngraver`, verifying carry of tempo/score/verdicts and the synchronous-fallback path when the worker can't start.
2. **RtMidi failure regression** — simulate the CoreMIDI client-creation throw fixed in `28a5801`; assert graceful fallback to keyboard input, no `std::terminate`.
3. **WindowEngraver race suite under TSan** — request/cancel/shutdown interleavings on the background Verovio toolkit; this is the newest thread in the codebase and TSan CI never exercises it under contention.
4. **Progress-store crash safety** — kill mid-write around the tmp+rename path; assert last-good JSON survives, unknown fields preserved, and a hand-corrupted file degrades to fresh-start with a warning (matches the corrupt-config recovery pattern already tested elsewhere).
5. **Hostile `.mxl` inputs** — truncated zip, oversized decompression, wrong central directory: importer must fail with a message, not crash or hang.
6. **SoundfontSynth golden-WAV test** — strike/release sample positions and instrument-switch click-freeness via the existing `wav_reader` support, like the metronome tests.
7. **macOS session-attach coverage** — un-exclude `session_attach_tests.cpp` on Apple (Catch2 upgrade or a local StringMaker shim); pending #15.
8. **SourceSlicer corpus equivalence fuzz** — random bar windows over several real MusicXML pieces asserting engrave-equivalence with the monolith (generalizes the Grieg measure-53 regression that was fixed by hand).
9. **WeatherService with an injected fetcher** — geocode/JSON parse, malformed payloads, curl-missing; requires adding the seam, which is the point.
10. **Workspace-invariant stress** — scripted create/close/move/activate workspace sequences asserting focused-host/input-routing consistency; gives pending bug #13 (*active-workspace-invariant*) a reproducer harness.

## 10 Worst Features (cost/benefit and maintenance drag)

1. **WeatherService / weather pill** — a network subprocess (`popen curl`) with hand-rolled JSON parsing inside a terminal emulator, undocumented config key, zero tests; the poster child for pending #49's network-privacy concern.
2. **BioView organism mode** — an entire procedural cell-biology renderer (organelles, DNA rungs, blood vessels) duplicating MegaCity's semantic-snapshot purpose with a second metaphor; enormous documented surface, niche insight.
3. **SatView solar-system expansion** — 20 major moons, Saturn ring bands, procedural moon textures, Milky Way skybox, Mars landing sites: a planetarium grafted onto a "satellite overview" host; largest single maintenance surface in the repo (5,284-line host, ~15 catalogs/pipelines/scripts).
4. **SatView lunar surface catalog** — 70 LROC-confirmed artifacts with GCAT enrichment and disposition overlays; beautiful data engineering serving an audience of approximately one.
5. **Live cloud-map fetching** — a background download service tracking EUMETSAT's three-hour cadence so the globe's clouds are current; real-time weather on a decorative shell is complexity with no failure-free mode.
6. **MegaCity LCOV coverage city** — importing lcov tracefiles to light up buildings duplicates what `genhtml`/editors already do, and drags coverage-format parsing into the renderer's orbit.
7. **MegaCity deep-tuning UI** — stacked struct plates, hex/oct shape thresholds, middle-strip push, alternate darken, sign min-width-per-char: dozens of persisted knobs for a visualization, each a config key and a parity risk forever.
8. **`print_pane`** — macOS-only CoreGraphics/PDFKit printing of a pane screenshot; single-platform, single-purpose code in generic `draxul-runtime-support`, contradicting the repo's otherwise-strict parity ethic.
9. **`--persistent-app` detach/reattach machinery** — nine CLI flags and an attach server whose bug tail (pending #00, #01, #02, #24, #25) keeps growing; the saved-topology restore path delivers most of the value at a fraction of the complexity.
10. **Kanban host** — a bespoke grid-rendered kanban board (5 libraries' worth of layout/navigation/store code) for what is ultimately a folder of Markdown files that every other pane type can already edit.

---

**Bottom line:** Draxul's engineering *practices* (tests, DI, comments, work-item discipline) are excellent and the host/render-pass abstractions have genuinely absorbed enormous feature growth. The risks are concentration, not quality: three god hosts, one duplicated GPU layer, one monolithic test binary, and a documentation map a year behind the territory. For a multi-agent future, the highest-leverage moves in order are: split the test binary (pending #35), regenerate the architecture docs from the real tree, decompose satview/megacity/score hosts (pending #26/#27), and build the shared GPU-resource layer (pending #28) before the next dual-backend feature lands.