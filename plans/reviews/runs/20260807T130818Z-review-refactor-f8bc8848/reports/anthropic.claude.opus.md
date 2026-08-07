I explored the tree directly (root/`app`/`libs`/`modules`/`shaders`/`tests`/`scripts`, all `CMakeLists.txt`, `cmake/*.cmake`, CI, `do.py`, both `AGENTS.md` files) and cross-checked every proposal against the current CMake target graph and against `kanban/pending`, `kanban/ice-box`, and `kanban/done` so nothing already tracked is repeated.

---

# Draxul Refactoring Review — Modularity, Testability, and Parallel-Agent Safety

## Scope and method

Verified against the tree as it exists on disk: 26 `libs/` targets + 20 product-module targets + `draxul-app` + `draxul` + 7 focused test executables, `cmake/CheckDependencyBoundaries.cmake`, `.github/workflows/build.yml`, `do.py` (4,835 lines), `t.bat`/`t.sh`/`scripts/run_tests.*`.

**Deliberately excluded as already tracked** (checked and confirmed still open/valid): internal-target build policy (`kanban/pending/00 …`), `app/CMakeLists.txt` ownership (`01 …`), host-layer splits (`02 …`), Nvim/protocol transport split (`03 …`), Kanban core/host (`04 …`), Megacity model library (`05 …`), SatView scene/host split (`06 …`), ScoreView overlay boundary (`07 …`), guidance + `do.py test --label` (`08 …`), App tab/session controllers (`ice-box/22 app-tab-session-controllers`), overlay registry (`ice-box/125`), Metal renderer internals (`ice-box/25`), core/extras repo split (`ice-box/23`), `UiRequestWorker` (`ice-box/23 uirequestworker-evaluate`), shared GPU resource helpers (`done/28`), modular test targets (`done/35`).

Note in particular: `app/app.cpp`'s ~1,200-line remote-topology reconciliation block (`apply_remote_topology_spaces` :3300 → `flush_pending_remote_split_ratio` :4212) is the single largest remaining app-layer mass, but `ice-box/22 app-tab-session-controllers -refactor.md` already owns that boundary. It is called out here only as sequencing context, not as a new finding.

---

## Findings

### F1 — `ServerKernel::Impl` is a five-responsibility god object with a 975-line method dispatcher

**Priority:** P1 (highest leverage untracked item)

**Location**
- `libs/draxul-server/src/server_kernel.cpp` (3,592 lines)
- `ServerKernel::Impl` class body: lines 339–556 (47 member functions, 21 data members, nested `ServerSession`/`ServerTerminalEndpoint`/`ClientRegistration`/`CheckpointTask`)
- `ServerKernel::Impl::handle_request` — lines 1899–2874 (**975 lines, 15 top-level `request.method ==` branches**)
- `status_snapshot` :2874–3140 (266 lines), `run_until_stopped` :3216–3528 (312), `prepare_session_restore` :773–987, `initialize_session` :987–1199
- `tests/server_kernel_tests.cpp` — 4,928 lines (largest test file in the repo)

**Current structural problem**
One private `Impl` in one `.cpp` owns five independent concerns that share nothing but the `mutex`: (a) singleton/discovery markers (`publish_starting_marker`, `published_identity_matches`, `remove_all_starting_markers`), (b) the client registry and token authentication (`register_client_hello`, `authenticate_or_touch_client`, `prune_inactive_clients`, `client_sessions`), (c) session restore/checkpoint persistence (`prepare_session_restore`, `checkpoint_session`, `collect_checkpoint_results`, `wait_for_checkpoint_tasks`), (d) terminal-endpoint lifecycle, and (e) JSON method routing. `handle_request` inlines the full body of each method — the agent block alone runs :2098–2574 (~475 lines), `pane.read` :2718–2821, `server.shutdown` :2821+. There is already a `ServerAgentService` (`src/server_agent_service.cpp`) and a `TopologyService`, so agent/topology logic is *split across* the service and the kernel's dispatcher rather than owned by one. Because everything is `Impl`-private, no piece is reachable except by constructing and running a whole kernel, which is why the test file is 4,928 lines of end-to-end control-plane traffic.

**Proposed boundary** — three internal units inside the existing `draxul-server` target, no new public headers:
1. `src/server_client_registry.{h,cpp}` — `ClientRegistration`, hello/token registration, activity touch, pruning, client↔session bookkeeping. Pure value logic over an injected `steady_clock::time_point`.
2. `src/server_session_store.{h,cpp}` — `ServerSession` record, restore preparation, checkpoint scheduling/collection/wait. Takes the `checkpoint_save` callable already present in `ServerKernelOptions`.
3. `src/server_method_router.{h,cpp}` — a `method → handler` table replacing the if-chain. Handlers stay free functions taking a narrow `KernelFacade&` (the ~10 operations the handlers actually need), so a handler can be exercised against a stub facade.

**Dependency shape**
- Callers: `ServerKernel::Impl` only. Nothing new is exported from `include/draxul/server_kernel.h`.
- Dependencies: registry → `<chrono>` + `draxul-protocol` values; store → `draxul-session-model` + `draxul-protocol`; router → `nlohmann_json` + `draxul-control` (`ControlRequest`/`ControlMethodResult`).
- Implementation-private: the `mutex`, `ControlServer`, terminal endpoints, `condition_variable` all stay in `Impl`.

**Migration path**
1. Move `ClientRegistration` + the six client functions verbatim into `server_client_registry.cpp`; `Impl` holds one `ServerClientRegistry` member. Build + full `ctest`.
2. Move `ServerSession` and checkpoint/restore functions into `server_session_store.cpp` the same way.
3. Convert `handle_request` mechanically, one branch at a time, into router entries — each commit moves one method body and leaves the if-chain fallback in place until the table is complete.
4. Only then add a `draxul-server-test-internals` INTERFACE target (mirroring the existing `draxul-renderer-test-internals` / `draxul-megacity-test-internals` pattern in `libs/draxul-renderer/CMakeLists.txt:113`) exposing `src/` to tests.

**Testing improvement**
Client-limit, token-rebinding, and activity-timeout cases become table-driven unit tests with a fake clock (`tests/support/fake_clock.h` already exists). Checkpoint durability/corrupt-archive cases test against a temp dir (`tests/support/temp_dir.h`) without a control socket. Each router entry gets an argument-validation test. Expect the majority of `server_kernel_tests.cpp` to become three focused files.

**Agent-work benefit**
Today any agent touching server sessions, clients, agents, or panes edits the same 975-line function and the same 4,928-line test file — guaranteed conflicts. After the split, "add a control method" touches one small router file, and "change checkpoint policy" touches one store file.

**Risks and prerequisites**
Lock discipline is the real risk: `mutex` is held across most of `handle_request`. Each extracted unit must document whether it expects the caller to hold the lock; do not introduce a second mutex. No platform parity concern — `draxul-server` has only 6 `#ifdef _WIN32` sites, all in process/marker code that stays in `Impl`.

---

### F2 — Nothing verifies that the optional product modules are actually removable

**Priority:** P1

**Location**
- `.github/workflows/build.yml` — exactly two jobs (`build-windows`, `build-macos`), each running `scripts/run_tests.{bat,sh}` with default options
- `CMakeLists.txt:33–41` — `DRAXUL_ENABLE_MEGACITY`/`SATVIEW` default `ON`, `SCOREVIEW` defaults `ON` on Win/macOS
- `modules/megacity/AGENTS.md:62` — *"After build-wiring changes, configure and build once with Megacity enabled and verify that configuration/build still succeeds with `-DDRAXUL_ENABLE_MEGACITY=OFF`"* — a manual instruction with no automation
- `modules/megacity/CMakeLists.txt:1–7` and `modules/satview/CMakeLists.txt:1–9` both assert isolation as the module's defining property

**Current structural problem**
The optional-module architecture — the reason `draxul-app` deliberately does not link `draxul-megacity` (`CMakeLists.txt:342–344`), the reason `main.cpp` uses `#ifdef DRAXUL_ENABLE_*` provider registration, and the precondition for `ice-box/23 core-extra-repo-split` — is enforced by nothing but reviewer discipline. CI never configures with an option `OFF`. The root CMake's per-option sanitizer/coverage blocks (`CMakeLists.txt:197–247`) are also only exercised in the all-`ON` configuration, so a target name typo inside an `if(DRAXUL_ENABLE_SATVIEW)` branch is caught but a *missing* guard elsewhere is not. F7 below is a concrete instance of the drift this permits.

**Proposed boundary**
Add one cheap CI leg — configure-only, no build — that proves the guards hold:

```
cmake -B build-min -DDRAXUL_ENABLE_MEGACITY=OFF -DDRAXUL_ENABLE_SATVIEW=OFF \
      -DDRAXUL_ENABLE_SCOREVIEW=OFF -DBUILD_TESTING=ON
```

plus, on one platform only, an actual `--target draxul draxul-test-core` build of that configuration. Expose it as `python do.py check-minimal` so agents can run the same gate locally.

**Dependency shape**
Pure CI/tooling. No source changes. `cmake/CheckDependencyBoundaries.cmake` already runs at configure time, so the minimal configure also re-validates the foundation boundaries under the reduced graph.

**Migration path**
1. Add the `configure-minimal` job to `.github/workflows/build.yml` as a third job (fast: no Vulkan SDK needed on the macOS leg, no test run).
2. Fix whatever it reports (expect the `tests/CMakeLists.txt` classification globs and the root sanitizer loops to need small guards).
3. Add the `do.py` subcommand and reference it from `modules/megacity/AGENTS.md` in place of the manual instruction.

**Testing improvement**
Turns an unverifiable architectural claim into a build-time contract. Also gives the future extras-repo split (`ice-box/23`) a working precondition instead of a discovery exercise.

**Agent-work benefit**
An agent working inside `modules/satview/` currently cannot cheaply prove it has not leaked into the core; this gives it a 60-second local command. It also makes "module removability" a shared, non-negotiable rule rather than per-module folklore that only Megacity documents.

**Risks and prerequisites**
Adds CI minutes (small — configure only). Likely surfaces existing latent failures; land those fixes before making the job required.

---

### F3 — `draxul-control` compiles both platform transports into one translation unit

**Priority:** P1

**Location**
- `libs/draxul-control/src/control_plane.cpp` — 1,944 lines, **50 preprocessor directives**, the highest `#ifdef` density in `libs/`
- Anonymous namespace of platform helpers: lines 48–940 (~890 lines)
- Two complete `ControlServer::Impl::run(std::stop_token)` bodies: Windows named pipe at **:1370**, POSIX `AF_UNIX` socket at **:1532**, separated by `#ifdef _WIN32` / `#else` at :1368/:1530
- `ControlClient::request` :1732–1944, also branched internally
- `libs/draxul-control/CMakeLists.txt` — a single unconditional source, `advapi32` linked only on Win32

**Current structural problem**
Framing/dispatch logic, endpoint-lock acquisition, metadata-file handling, and two entirely different OS transports live in one file with no seam between them. Neither half can be read, reviewed, or tested independently, and a macOS agent editing the socket path must scroll past 890 lines of Win32 security-descriptor code. This is the transport under both `draxul-client` and `draxul-server`, so it sits on the critical path of the session/topology work in `kanban/pending/09`–`14`.

The repo already has the correct pattern one directory over: `libs/draxul-terminal-process/CMakeLists.txt` selects `src/conpty_process.cpp` vs `src/unix_pty_process.cpp` by `if(WIN32)`, with matching public headers and near-zero `#ifdef`s in either file.

**Proposed boundary**
Mirror `draxul-terminal-process` exactly:
- `src/control_transport.h` — private interface: listen/accept/read-frame/write-frame/close, plus endpoint-lock acquire/release.
- `src/control_transport_win32.cpp` / `src/control_transport_posix.cpp` — one platform each, selected in CMake.
- `src/control_plane.cpp` keeps `ControlRequest`/`ControlMethodResult`, framing, `dispatch`, `process_pending`, `complete_pending`, and the `run` loop **once**, written against the transport interface.

**Dependency shape**
- Callers: `ControlServer`/`ControlClient` implementation only; `include/draxul/control_plane.h` is unchanged.
- Dependencies: transport headers depend on `<filesystem>`/`<string>` only; Win32 TU adds `advapi32`, POSIX TU adds nothing.
- Implementation-private: `HANDLE`, `SECURITY_DESCRIPTOR`, `sockaddr_un`, and all fd/handle types never cross the interface.

**Migration path**
1. Extract the anonymous-namespace helpers (:48–940) into the two platform TUs unchanged, keeping identical signatures. Build both platforms.
2. Define the transport interface and reimplement the two `run` bodies as one loop over it — this is the only behavioral-risk step; do it alone.
3. Do the same for `ControlClient::request`.
4. Add `draxul-control-transport-fake` in tests to drive framing without a real pipe/socket.

**Testing improvement**
`tests/control_plane_tests.cpp` (1,371 lines) currently needs a live endpoint. A fake transport lets frame-boundary, oversized-payload, malformed-JSON, and stop-token cases run in-process on both platforms. Note that `kanban/pending/09 macos-remote-terminal-channel -bug.md` is a macOS-only channel bug — the seam this creates is exactly what makes that class of bug reproducible in a unit test.

**Agent-work benefit**
Windows and macOS control-plane work stop sharing a file. The framing logic becomes reviewable without OS API knowledge.

**Risks and prerequisites**
Highest-parity-risk item in this review: the two `run` loops may have quietly diverged in timeout, error-code, and reconnect handling. Step 1 must be a pure move with no unification; diff the two loops explicitly and record any behavioral difference as a decision before step 2. Requires a real build+test pass on both platforms.

---

### F4 — Root `shaders/` is a flat, unowned directory compiled globally on Windows

**Priority:** P1

**Location**
- `shaders/` — 64 `.vert`/`.frag` files in one flat directory: **satview 36, megacity 16**, markdown 4, grid 4, gui 2, nanovg 2 (**52 of 64 belong to optional modules**), plus 8 `.metal`/`.glsl`/`.h` files
- `cmake/CompileShaders.cmake:11–14` — `file(GLOB … *.vert *.frag)`, unconditional
- `cmake/CompileShaders.cmake:16,25` — `SHADER_GLSL_INCLUDES` globs every `.glsl`/`.h` and makes it a `DEPENDS` of **every** shader
- `CMakeLists.txt:137–141` — `include(cmake/CompileShaders.cmake)` runs before any module option is consulted; `CMakeLists.txt:468–475` — `add_dependencies(draxul compile_shaders)` + `copy_directory` of the whole output dir
- `cmake/CompileShaders_Metal.cmake` — 156 lines, the same 19-line `add_custom_command`×2 + `add_custom_target` block repeated **7 times** (grid, gui, markdown, megacity_scene, megacity_gbuffer, megacity_ao, satview_scene)
- `CMakeLists.txt:421–466` — a `draxul_stage_metal_shader` helper plus a hand-maintained `DRAXUL_METAL_SHADER_DEPS` list

**Current structural problem**
Three distinct defects in one place:
1. **No ownership.** `modules/megacity/AGENTS.md:36` states outright that "Megacity shader sources live in the repository-root `shaders/` directory, and their compilation/copy wiring lives in the top-level CMake files." A SatView agent and a Megacity agent both edit `shaders/` and both edit root CMake to wire a new shader — the two highest-traffic shared files in the repo.
2. **No option gating on Windows.** With `-DDRAXUL_ENABLE_SATVIEW=OFF`, all 36 SatView shaders still compile and are still copied next to `draxul.exe`. macOS gets this right (`compile_satview_shaders` is appended to `DRAXUL_METAL_SHADER_DEPS` only under `if(DRAXUL_ENABLE_SATVIEW)`, `CMakeLists.txt:459–465`) — the two backends have opposite policies for the same asset class.
3. **Maximal false dependencies.** Because `SHADER_GLSL_INCLUDES` globs the whole directory, editing `shaders/satview_sky_projection.glsl` re-runs `glslc` on all 64 shaders, including `grid_bg.vert` and `markdown_rect.frag`.

**Proposed boundary**
A single reusable CMake function plus per-module shader ownership:
- `cmake/DraxulShaders.cmake` exposing `draxul_add_shader_library(<name> SOURCES … INCLUDES … [METAL <file>])` that emits the SPIR-V or metallib rule and the staging step for the host platform. This collapses `CompileShaders_Metal.cmake` from 156 lines to ~40 and gives Windows the same per-group granularity macOS already has.
- Move shader sources next to their owner: `shaders/` keeps only `grid_*`, `gui_*`, `nanovg*`, and the genuinely shared `decoration_constants*.h` / `quad_offsets_shared.h`; `modules/satview/draxul-satview-renderer/shaders/`, `modules/megacity/draxul-codeviz-renderer/shaders/`, and `modules/markdown/draxul-markdown/shaders/` own the rest.
- Each owning library calls `draxul_add_shader_library` from its own `CMakeLists.txt` and declares `add_dependencies` on itself, so the option gate is automatic.

**Dependency shape**
- Callers: each renderer-owning target's `CMakeLists.txt`.
- Explicit `INCLUDES` lists replace the directory glob, so `satview_sky_projection.glsl` is a dependency of exactly the SatView shaders that `#include` it.
- Runtime lookup is unchanged: everything still lands in `$<TARGET_FILE_DIR:draxul>/shaders`, so `vk_pipeline.cpp:45`, `markdown_render_pass_vk.cpp:553`, `satview_render_vk.cpp:1437`, and `codeviz_render_vk.cpp:1347` need no edits.

**Migration path**
1. Add `cmake/DraxulShaders.cmake` and re-express the seven existing Metal groups through it — pure de-duplication, output paths identical.
2. Give the Windows path the same grouping (`grid`, `gui`, `nanovg`, `markdown`, `megacity`, `satview`), still from root, still all `ON`. Verify `.spv` output set is byte-identical.
3. Gate the `megacity`/`satview` groups on their options. Verify with the F2 minimal configure.
4. Move files module-by-module (SatView first — largest win, most isolated). One module per commit.

**Testing improvement**
`tests/shader_abi_parity_tests.cpp` and `shaders/contracts/grid_push_constants.toml` already assert the grid ABI; per-module shader groups make it natural to add one contract file per renderer without growing a shared fixture. `python do.py renderall` gains meaning per module.

**Agent-work benefit**
The single largest reduction in cross-module collision surface available here: two of the biggest product modules stop editing the root shader directory and root CMake for routine renderer work. Windows incremental builds for shader-touching work drop from 64 `glslc` invocations to the handful in the edited group.

**Risks and prerequisites**
Vulkan/Metal parity is the whole point — steps 1 and 2 must not change any output filename, or the runtime `load_shader` calls listed above break silently at startup rather than at build time. Add a temporary configure-time assertion that the emitted output-file set matches the previous list before deleting the old glob. Requires a render-snapshot pass (`python do.py renderall`) on both platforms.

---

### F5 — Pure, GPU-free shell-layout logic is trapped inside the `draxul-app` target

**Priority:** P1

**Location**
- `app/split_tree.{h,cpp}` (770 lines) — includes only `draxul/pane_descriptor.h`, `draxul/session_model.h`, `<algorithm>`, `draxul/log.h`, `draxul/perf_timing.h`
- `app/chrome_layout.{h,cpp}` (689+238) — includes `draxul/app_config_types.h`, `draxul/system_resource_monitor.h`, `draxul/unicode.h`
- `app/app_shell_layout.cpp` (91) — includes `<algorithm>` only
- `app/chrome_pill.cpp` (103), `app/rename_editor.cpp` (190), `app/fuzzy_match.{h,cpp}` (144) — `fuzzy_match.h` has **zero** draxul includes
- `CMakeLists.txt:281–340` — all of these compile into `draxul-app`, which links 17 libraries including `draxul-renderer`, `draxul-nanovg`, `draxul-nvim`, `draxul-host`
- `tests/CMakeLists.txt:274–292` — `draxul-test-app` links `draxul-app` + `draxul-markdown-host` + `draxul-kanban` + `draxul-megacity` + `draxul-satview-host` + `draxul-scoreview-host`
- Affected suites, all self-contained (verified: each includes exactly one app header plus Catch2): `tests/split_tree_tests.cpp`, `tests/chrome_layout_tests.cpp`, `tests/fuzzy_match_tests.cpp`, `tests/app_shell_layout_tests.cpp`

**Current structural problem**
`app/` is 22,704 lines in one flat directory behind one target with `target_include_directories(draxul-app PUBLIC app)` — every one of its ~30 headers is visible to every consumer, and every edit rebuilds the whole library. Roughly 2,100 of those lines are deterministic integer geometry and string matching with no window, no GPU, no host, and no frame loop. To run `fuzzy_match_tests.cpp` — a function with no draxul dependencies at all — the build must link Verovio, a soundfont-backed audio stack, Tree-sitter, and both GPU backends.

**Proposed boundary**
`libs/draxul-shell-layout` — one small static library:

| Public header | Contents |
|---|---|
| `include/draxul/split_tree.h` | `SplitTree`, `FocusDirection`, `DividerRect` |
| `include/draxul/chrome_layout.h` | `ChromeLayout`, `ChromeHitKind`, chrome constants |
| `include/draxul/app_shell_layout.h` | `AppShellLayout` |
| `include/draxul/chrome_pill.h` | pill geometry |
| `include/draxul/fuzzy_match.h` | `fuzzy_match` |
| `include/draxul/rename_editor.h` | `RenameEditor` |

**Dependency shape**
- Callers: `draxul-app` (`app.cpp`, `pane_manager.cpp`, `chrome_host.cpp`, `command_palette.cpp`, `tab_controller.cpp`), `draxul-test-core`.
- Dependencies: `draxul-types` (log, unicode, `PaneDescriptor` after the prerequisite below), `draxul-session-model` (`session_model.h`), `draxul-config` (`app_config_types.h`), `draxul-runtime-support` (`system_resource_monitor.h`), `draxul-performance` PRIVATE. **No** renderer, window, host, SDL, or ImGui.
- Implementation-private: `SplitTree::Node` stays in the `.cpp` where it already is (`split_tree.cpp:15`).

**Prerequisite (one line, worth doing regardless)**
`PaneDescriptor` (`libs/draxul-renderer/include/draxul/pane_descriptor.h`) is two `glm::ivec2` fields. Living in `draxul-renderer`'s public tree, it drags `draxul-renderer` — and transitively SDL3 and ImGui — into anything that wants split geometry. Move it to `draxul-types` (which already links `glm::glm`) and leave a forwarding header for one release. This is what makes the library's dependency list honest.

**Migration path**
1. Move `PaneDescriptor` to `draxul-types`; forwarding header at the old path. Full build.
2. Create the target with `fuzzy_match` + `app_shell_layout` only (zero-risk, no draxul deps). Add `draxul-shell-layout` to `draxul-app`'s PUBLIC links, delete the two files from the `draxul-app` source list, reclassify the two test files out of `DRAXUL_APP_TEST_SOURCES` in `tests/CMakeLists.txt:25–59`, and add the target to `draxul-test-core`'s link list.
3. Move `split_tree` (largest, still dependency-clean).
4. Move `chrome_layout` + `chrome_pill` + `rename_editor` together — they are mutually coupled via `chrome_layout.h`'s includes.

**Testing improvement**
Four suites move from the 2-shard, all-products `draxul-test-app` into the 4-shard `draxul-test-core`. Split/focus/divider geometry becomes testable without Vulkan or Metal available, which also unblocks `ice-box/08 splittree-min-pane-size -test.md` and `ice-box/14 panemanager-split-close-stress -test.md` as cheap unit work.

**Agent-work benefit**
Removes ~2,100 lines from the highest-contention target in the repo and gives layout work a named owner with a 4-line dependency list. An agent doing chrome/tab-bar geometry no longer needs `app.h` (375 lines, 26 includes) or `App` (≈72 data members) in context.

**Risks and prerequisites**
Low. Everything moved is deterministic and already header-isolated. The `PaneDescriptor` move touches many files — do it alone and mechanically. No platform-specific code is involved; both backends are unaffected.

---

### F6 — `draxul_main()` is a ~630-line mode dispatcher in the one translation unit no test can link

**Priority:** P1

**Location**
- `app/main.cpp:534–1162` — `draxul_main(std::vector<std::string>)`, ~628 lines, 13 `#ifdef` sites
- `CMakeLists.txt:349–352` — `main.cpp` is compiled into the `draxul` **executable**, not into `draxul-app`
- Mode branches inside it: server mode (`run_server_mode` :237–~330), macOS bundle re-exec (:598–629), host-provider registration (:663–671), screenshot, `--render-test`, `--bless-render-test` (:931–1104), `--gui-action`, `--control-cli`, `--smoke-test`
- `tests/CMakeLists.txt:274` — `draxul-test-app` links `draxul-app`, so nothing in `main.cpp` is reachable from any test
- `tests/cli_args_tests.cpp` covers `app/cli_args.cpp` (469 lines) — parsing is already extracted and tested; *acting* on the parse is not

**Current structural problem**
Argument parsing was correctly pulled into `cli_args.{h,cpp}` and is tested. Everything that decides *what to do with* a parsed `ParsedArgs` — mode precedence, exit codes, failure messages, the render-test/bless path, the control-CLI path, and which of ten mutually exclusive modes wins — remains in the executable's `main.cpp` and has zero test coverage by construction. Mode-selection regressions are only observable by launching the binary. This also concentrates unrelated platform concerns (Win32 console handles, macOS bundle re-exec, POSIX signal handlers) in the same function as product-mode routing.

**Proposed boundary**
Add `app/app_entry.{h,cpp}` to the **`draxul-app`** library:

```cpp
struct EntryEnvironment {              // injectable; production fills from the real process
    std::filesystem::path executable_dir, runtime_dir;
    std::function<int(const ParsedArgs&)> run_server;
    std::function<int(const ParsedArgs&)> run_app;      // App construction stays out
    // …one callable per mode
};
int run_entry(const ParsedArgs& parsed, EntryEnvironment env);
```

`main.cpp` shrinks to: platform bootstrap (console/bundle/signals) → `parse_args` → build the production `EntryEnvironment` → `run_entry`.

**Dependency shape**
- Callers: `app/main.cpp` and `tests/`.
- Dependencies: `app/cli_args.h` plus `<functional>`/`<filesystem>` — nothing else. `App`, `ServerKernel`, and the render-test driver are reached only through the injected callables, so `app_entry.cpp` stays free of them.
- Implementation-private: none; the whole point is that the mode table is inspectable.

**Migration path**
1. Introduce `run_entry` handling only the unambiguous non-GPU modes (`--control-cli`, `--server`, help/version). `main.cpp` delegates those and keeps the rest.
2. Add `tests/app_entry_tests.cpp` to `DRAXUL_APP_TEST_SOURCES` covering mode precedence and exit codes with stub callables.
3. Migrate the remaining modes one per commit; the render-test/bless branches (`:931–1104`) last, since they are `#ifdef DRAXUL_ENABLE_RENDER_TESTS`-gated and need both option states checked.
4. Leave platform bootstrap in `main.cpp` — it is genuinely executable-scoped.

**Testing improvement**
First-ever coverage of mode precedence and exit codes, including the option-disabled paths (`#else` branches at `:964`, `:1043`, `:1058`, `:1104`) that currently no build configuration exercises in CI.

**Agent-work benefit**
`ice-box/54 bless-render-test-flag-ci` becomes a small change to a tested table instead of a surgical edit to a 628-line function. Adding a CLI mode stops requiring an untestable edit to the executable TU.

**Risks and prerequisites**
Mode precedence is currently *implicit in statement order*. Step 1 must first write down the existing precedence as a table and assert it, before any reordering. Do not fold the `DRAXUL_ENABLE_*` host-provider registration blocks (`:663–671`) into this work — `ice-box/23 core-extra-repo-split` Phase 0 owns that seam.

---

### F7 — Dead `VkCubePass` in the core Vulkan renderer, with matching stale architecture documentation

**Priority:** P2

**Location**
- `libs/draxul-renderer/src/vulkan/vk_cube_pass.{h,cpp}` — compiled into `draxul-renderer-vulkan` (`libs/draxul-renderer/CMakeLists.txt:74`); `VkCubePass::create`/`destroy` are **referenced from nowhere outside their own TU** (verified by repo-wide grep)
- `vk_cube_pass.h:9` — comment cites `megacity_render_vk.cpp`, a file that no longer exists
- `modules/megacity/draxul-megacity/src/cube_render_pass.h` — declares `CubeRenderPass : public IRenderPass`, has **no `.cpp`** and is absent from `MEGACITY_SOURCES` (`modules/megacity/draxul-megacity/CMakeLists.txt:3–23`)
- `shaders/megacity_cube.vert` / `megacity_cube.frag` — compiled on every Windows build (see F4), loaded only by the dead `vk_cube_pass.cpp:47–48`
- Documentation asserting this is live architecture: `CLAUDE.md` "Key abstractions" (`I3DRenderer`, `I3DHost`, `IGridHost`, `register_render_pass`, `attach_3d_renderer`, "`MegaCityHost` inherits `I3DHost` directly and registers a `CubeRenderPass`"); `docs/module-map.md:140–151` (the `IBaseRenderer → I3DRenderer → IGridRenderer` diagram and `register_render_pass`/`unregister_render_pass`); `CMakeLists.txt:28–32` (comment about "the unconditional `dynamic_cast` / `I3DPassProvider` surface in IRenderer")

**Current structural problem**
Repo-wide greps find **zero** definitions or uses of `I3DRenderer`, `I3DHost`, `IGridHost`, `I3DPassProvider`, `register_render_pass`, `unregister_render_pass`, or `attach_3d_renderer`. The real hierarchy is two tiers — `IBaseRenderer` → `IGridRenderer` (`libs/draxul-renderer/include/draxul/renderer.h:49`) — with passes recorded per frame via `IBaseRenderer::record_render_pass(IRenderPass&, const RenderViewport&)` (`base_renderer.h:35`), and `MegaCityHost final : public IHost` (`megacity_host.h:43`). An agent reading `CLAUDE.md` will look for a registration API that does not exist, and one reading `vk_cube_pass.h` will conclude that Megacity legitimately owns pipeline code inside the core renderer — the exact boundary `modules/megacity/CMakeLists.txt:1–7` says must not exist.

**Proposed boundary**
Delete `libs/draxul-renderer/src/vulkan/vk_cube_pass.{h,cpp}`, `modules/megacity/draxul-megacity/src/cube_render_pass.h`, and `shaders/megacity_cube.{vert,frag}`. Correct the renderer/host hierarchy sections of `CLAUDE.md` and `docs/module-map.md` to the two-tier `IBaseRenderer → IGridRenderer` shape with `record_render_pass`, and drop the `I3DPassProvider` sentence from `CMakeLists.txt:28–32`.

**Dependency shape**
Removal only. `draxul-renderer-vulkan` loses one source; no target's link list changes. `docs/module-map.md` is *not* in the inventory list of `kanban/pending/08 agent-guidance-label-validation -refactor.md` (which names `CLAUDE.md`, root/nested `AGENTS.md`, README, `do.py`, and test wrappers) even though `CLAUDE.md` designates it canonical for the graph — add it to that card's scope.

**Migration path**
Single commit: delete the four files, run `cmake --build build --target draxul draxul-tests` on both platforms (the Metal side has no cube equivalent at all — `metal_renderer.mm` contains no cube code — so macOS is unaffected), then correct the three documentation sites.

**Testing improvement**
Removes an untested, unreachable code path from a core library. Recovers two shader compilations per Windows build.

**Agent-work benefit**
Eliminates a false precedent for product-specific pipeline code inside `draxul-renderer`, and stops new render-pass work from starting with a search for a nonexistent registration API.

**Risks and prerequisites**
Confirm with `git log` that this is abandoned rather than in-flight work before deleting. If a spinning-cube demo is still wanted, it belongs entirely inside `draxul-codeviz-renderer` alongside `CodeVizScenePass`. Because `record_render_pass` already exists on `IBaseRenderer`, correcting the docs will also settle the "guard or remove the 3D surface when megacity is disabled" note in `CMakeLists.txt:31–32` — it is already gone.

---

### F8 — App test classification and test-target names are hand-maintained in shared files

**Priority:** P2

**Location**
- `tests/CMakeLists.txt:25–59` — `DRAXUL_APP_TEST_SOURCES`, an explicit **33-entry alphabetical list**
- `tests/CMakeLists.txt:60–92` — every other module uses a prefix `file(GLOB … CONFIGURE_DEPENDS)`: `markdown_*`/`kanban_*`, `megacity_*`/`treesitter*`, `satview_*`, `notation_*`/`scoreview_*`
- `tests/CMakeLists.txt:186–190` — `DRAXUL_TEST_SOURCE_BASELINE 190`, `DRAXUL_TEST_REGISTRATION_BASELINE 1815`, `DRAXUL_TEST_TAG_TOKEN_BASELINE 228`
- `do.py` (coverage command) — the seven focused executable names (`draxul-test-core`, `-app`, `-markdown-kanban`, `-megacity`, `-satview`, `-scoreview`, `-scoreview-host`) hardcoded a second time
- `tests/` — 190 `*_tests.cpp` in one flat directory

**Current structural problem**
The classification scheme is otherwise well designed — the partition assertions at `:103–128` and the case/tag-inventory check at `:175–181` are genuinely good safety nets, and the "unclassified lands in core and fails to link" default at `:16–19` is the right bias. But app tests are the one category with no naming convention, so every agent adding an app-level test edits the same 33-line list in the same shared file. That is a mechanical merge conflict for the most-touched test category in the repo. Separately, the seven target names exist in two places with nothing tying them together — adding an eighth focused target silently drops it from coverage reporting.

**Proposed boundary**
- Give app tests a convention so they can be globbed like every other module. Cheapest version: keep names, add `file(GLOB … app_*_tests.cpp chrome_*_tests.cpp)` and shrink the explicit list to genuine exceptions (`split_tree_tests.cpp`, `pane_manager_tests.cpp`, `session_state_tests.cpp`, …). Cleaner version, and the natural companion to F5: move app-owned suites into `tests/app/` and glob the subdirectory — the partition and inventory assertions then work unchanged on a per-directory basis.
- Publish the focused target list once, e.g. a `DRAXUL_TEST_TARGETS` cache variable written by `draxul_add_test_target`, and have `do.py` read it from `build/CMakeCache.txt` rather than restating it.

**Dependency shape**
CMake and tooling only; no source changes. `draxul_add_test_target` (`tests/CMakeLists.txt:211`) already centralizes target creation, so recording the name is a two-line addition.

**Migration path**
1. Add the `DRAXUL_TEST_TARGETS` cache variable and switch `do.py`'s coverage command to read it. Verify `python do.py coverage` on macOS produces the same LCOV.
2. Introduce the glob for the clearly-prefixed app suites and shrink the explicit list; the existing partition assertion at `:124–128` proves nothing moved.
3. Optionally introduce `tests/app/` alongside F5's `tests/` reclassification, so both land in one review.

**Testing improvement**
No coverage change; makes the classification self-maintaining and keeps the coverage report honest as targets are added by the pending host/Nvim/product cards.

**Agent-work benefit**
Removes a guaranteed conflict point from the file that *every* parallel agent adding a test must touch — directly the "hotspots where unrelated work collides" concern.

**Risks and prerequisites**
Globs must stay `CONFIGURE_DEPENDS` (they already are). Keep the `LESS`-based baselines at `:192–200` — they are the safety net that makes glob-based classification safe.

---

### F9 — `draxul-app-support` is an empty INTERFACE bundle that widens dependency fan-out

**Priority:** P2

**Location**
- `libs/draxul-app-support/` — contains **only** a `CMakeLists.txt`; no `include/`, no `src/`
- `libs/draxul-app-support/CMakeLists.txt:1–7` — `add_library(… INTERFACE)` re-exporting `draxul-config` + `draxul-runtime-support` + `draxul-render-test`
- Sole consumer: `tests/CMakeLists.txt:251` (`draxul-test-core`). `draxul-app` links those three targets directly (`CMakeLists.txt:329–331`) and does **not** use it
- `docs/module-map.md:214` describes it as "Interface target bundling reusable config/runtime/render-test dependencies"

**Current structural problem**
A named target that owns no code and has one consumer. It hides which of the three libraries `draxul-test-core` actually needs, and the name suggests an app-support layer that does not exist. This directly undercuts `cmake/CheckDependencyBoundaries.cmake`'s stated principle (`:4–7`): *"a transitive include/link path is not an ownership boundary."* The boundary checker itself never inspects this target.

**Proposed boundary**
Delete the directory and the `add_subdirectory(libs/draxul-app-support)` line (`CMakeLists.txt:168`); list whichever of the three `draxul-test-core` genuinely requires in `tests/CMakeLists.txt:248–269` directly. Remove the `docs/module-map.md:214` row.

**Dependency shape**
Removal only. One consumer to update.

**Migration path**
Single commit: replace `draxul-app-support` in `draxul-test-core`'s link list with the three targets, build, then drop any that turn out to be unnecessary. Delete the directory.

**Testing improvement**
Makes `draxul-test-core`'s real dependency surface visible, which is the input to any future decision about splitting the 4-shard core suite.

**Agent-work benefit**
One fewer target name to learn; removes a misleading entry from the module map an agent reads for orientation.

**Risks and prerequisites**
Trivial. Coordinate with `kanban/pending/00 internal-target-build-policy` — that card is auditing every target, and this one should be deleted rather than classified.

---

### F10 — `cmake/CheckDependencyBoundaries.cmake` protects the foundation but not the product modules

**Priority:** P2

**Location**
- `cmake/CheckDependencyBoundaries.cmake:21–93` — `draxul_check_direct_link` on 17 foundation edges; `draxul_reject_direct_links` on `draxul-terminal-core`, `draxul-terminal-process`, `draxul-protocol`, `draxul-client`, `draxul-server`, `draxul-host`
- `:71–92` — the product-leak scan runs over exactly 10 foundation targets
- Not covered by any check: `draxul-app`, `draxul-renderer`, `draxul-gui`, `draxul-ui`, `draxul-nanovg`, `draxul-config`, `draxul-font`, and **every** product-module target

**Current structural problem**
The checker is a genuinely strong asset and the right mechanism — but its coverage stops exactly where the recent structural work is happening. Three invariants stated in prose have no enforcement:
1. `CMakeLists.txt:342–344` — "`draxul-megacity` is intentionally NOT linked here… the terminal product library has zero megacity dependency." Nothing rejects `draxul-app → draxul-megacity`.
2. Product modules must not link each other. Nothing prevents `draxul-satview-host → draxul-megacity`.
3. `modules/megacity/AGENTS.md:7` — "core libraries and the terminal product must not acquire unguarded source, header, link, test, shader, or asset dependencies on this directory." Unenforced.
F7 is a concrete instance of drift that landed under exactly this gap.

**Proposed boundary**
Extend the existing function — no new mechanism:
- `draxul_reject_direct_links(draxul-app draxul-megacity draxul-satview-host draxul-scoreview-host)`
- A pairwise loop rejecting cross-product links among the `megacity`/`satview`/`score` target families, reusing the `_product_target_pattern` regex already at `:71–72`.
- Extend the foundation product-leak scan to `draxul-renderer`, `draxul-gui`, `draxul-ui`, `draxul-config`, `draxul-font`, `draxul-window`.

**Dependency shape**
Configure-time only. Must run inside the existing `if(DRAXUL_ENABLE_*)` guards since the optional targets may not exist — pair naturally with F2's minimal-configure leg, which exercises exactly those code paths.

**Migration path**
1. Add the `draxul-app` rejection (should pass immediately).
2. Add the cross-product loop; fix or explicitly record any hit.
3. Extend the foundation scan to the six UI/renderer targets last, since that is the most likely to surface an existing violation.

**Testing improvement**
Turns three prose invariants into configure-time failures. Complements the `link_isolation/` build-only contracts at `tests/CMakeLists.txt:399–432`, which prove *positive* single-target consumption; this proves the *negative* direction.

**Agent-work benefit**
An agent adding a link line gets an immediate, specific error naming the crossed boundary, instead of a reviewer catching it later or not at all.

**Risks and prerequisites**
Errors must name the rule, not just the edge — the existing messages (`:5–7`, `:15–18`) already do this well; match their tone. Guard every new check on the corresponding option.

---

### F11 — Per-library ownership contracts exist for 2 of 26 libraries

**Priority:** P2

**Location**
- `libs/draxul-gui/README.md` and `libs/draxul-ui/README.md` — the only per-library docs in `libs/` (verified: `find libs modules app -name "*.md"` returns exactly these two plus `modules/megacity/AGENTS.md`)
- Their CMakeLists carry matching inline contracts: `libs/draxul-gui/CMakeLists.txt:1–5` and `:20–24` ("Dependency contract: types…, font…, renderer…. Deliberately nothing else — this library must stay usable from pure unit tests without a window, GPU, or ImGui context"); `libs/draxul-ui/CMakeLists.txt:1–7` and `:18–20`
- Targets with no local statement of purpose, some with large surfaces: `draxul-server` (3,592-line kernel), `draxul-control`, `draxul-client` (7 sources), `draxul-host` (11 sources), `draxul-runtime-support` (9 public headers spanning printing, resource monitoring, and grid pipeline), `draxul-renderer`
- `modules/satview/` (5 targets, 17,935 lines) and `modules/score/` (5 targets, 12,625 lines) have no nested `AGENTS.md`, while the comparably sized `modules/megacity/` has an excellent one

**Current structural problem**
The gui/ui pair demonstrate the pattern that works: a short README stating what belongs here, what does not, and *why the dependency list is exactly this*, mirrored as a comment beside the target definition. That pattern was never propagated. `docs/module-map.md` carries one-line ownership rows, but they live far from the code and cannot be reviewed in the same diff as a `target_link_libraries` change — which is precisely how the F10 gaps and the F7 drift went unnoticed.

**Proposed boundary**
Two mechanical, low-cost steps:
1. Require a "Dependency contract" comment block beside every `target_link_libraries` call in `libs/` and `modules/`, in the exact form used by `draxul-gui`/`draxul-ui`. This is one paragraph per target, written where a reviewer will see it.
2. Add `modules/satview/AGENTS.md` and `modules/score/AGENTS.md` modeled on `modules/megacity/AGENTS.md` — the same six sections (Scope, Module Structure with the dependency arrow diagram, Rendering/backend parity, Threading and Lifetime, Build Wiring, Tests and Validation).

**Dependency shape**
Documentation only. Full READMEs are warranted for `draxul-server`, `draxul-control`, and `draxul-runtime-support`, whose boundaries are actively being reshaped by `kanban/pending/02`, `03`, and F1/F3 here.

**Migration path**
Write the contract comment as part of whatever structural card touches each target — `pending/02` for `draxul-host`, `pending/03` for `draxul-nvim`, F1 for `draxul-server`, F3 for `draxul-control`. Do not do it as a standalone doc sweep; a contract written apart from the boundary work is guesswork.

**Testing improvement**
None directly. It is the input that makes F10's automated checks writable — you cannot encode a boundary rule you have not stated.

**Agent-work benefit**
Cuts the context an agent needs before a bounded task: today, understanding whether a new dependency is legal in `draxul-runtime-support` requires reading nine public headers and the root CMake.

**Risks and prerequisites**
Coordinate with `kanban/pending/08 agent-guidance-label-validation` so the nested SatView/Score guides land once, from the final target names, rather than being written now and rewritten after `pending/02`/`03`. That card's boundary-verification item — *"Confirm SatView/Score rules that genuinely need nested scope rather than root duplication"* — already owns the *decision*; this finding supplies the evidence that the answer is yes for both, and adds the per-library contract comments, which that card does not cover.

---

## Proposed module / target map

Only the deltas from the current graph. `←` denotes a new or moved edge.

```text
libs/draxul-types            + PaneDescriptor  (moved from draxul-renderer public tree)   [F5 prereq]

libs/draxul-shell-layout     NEW static lib                                                [F5]
   public:  split_tree.h, chrome_layout.h, app_shell_layout.h,
            chrome_pill.h, fuzzy_match.h, rename_editor.h
   deps:    draxul-types, draxul-session-model, draxul-config,
            draxul-runtime-support   (+ draxul-performance PRIVATE)
   callers: draxul-app, draxul-test-core
   NOT:     renderer, window, host, SDL3, imgui

libs/draxul-control          src/control_transport.h                    (private iface)    [F3]
                           ← src/control_transport_win32.cpp   if(WIN32)
                           ← src/control_transport_posix.cpp   else()
   control_plane.cpp keeps framing/dispatch/run ONCE; public header unchanged

libs/draxul-server           ← src/server_client_registry.{h,cpp}                          [F1]
                             ← src/server_session_store.{h,cpp}
                             ← src/server_method_router.{h,cpp}
                             ← draxul-server-test-internals  (INTERFACE, mirrors
                                draxul-renderer-test-internals)

libs/draxul-renderer          − src/vulkan/vk_cube_pass.{h,cpp}          (dead)            [F7]
libs/draxul-app-support       DELETE (empty INTERFACE bundle)                              [F9]

app/                          ← app_entry.{h,cpp}  into draxul-app                         [F6]
                              main.cpp → platform bootstrap + parse + run_entry

cmake/DraxulShaders.cmake     NEW  draxul_add_shader_library(...)                          [F4]
   shaders/                   keeps grid_*, gui_*, nanovg*, *_shared.h, contracts/
   modules/*/…-renderer/shaders/   own satview_* (36), megacity_* (16), markdown_* (4)
   CompileShaders_Metal.cmake  156 lines → ~40

cmake/CheckDependencyBoundaries.cmake
   + reject  draxul-app → {megacity, satview-host, scoreview-host}                         [F10]
   + reject  cross-product links among megacity / satview / score families
   + extend  product-leak scan to renderer, gui, ui, config, font, window

.github/workflows/build.yml   + configure-minimal job (all DRAXUL_ENABLE_* = OFF)          [F2]
do.py                         + check-minimal;  read DRAXUL_TEST_TARGETS from cache        [F2, F8]
tests/CMakeLists.txt          app suites globbed (or tests/app/);  publish target list     [F8]
```

---

## Best isolated work packages for parallel agents

Ordered by what can start immediately versus what must wait. Packages within a wave touch disjoint files.

**Wave 1 — fully independent, start together (4 agents)**

| # | Package | Files touched | Blocks nothing |
|---|---|---|---|
| A | **F2** minimal-configure CI leg + `do.py check-minimal` | `.github/workflows/build.yml`, `do.py` | — |
| B | **F7** delete dead cube pass + correct hierarchy docs | 4 file deletions, `CLAUDE.md`, `docs/module-map.md`, `CMakeLists.txt:28–32` | — |
| C | **F9** delete `draxul-app-support` + **F8** step 1 (`DRAXUL_TEST_TARGETS`) | `libs/draxul-app-support/`, `CMakeLists.txt:168`, `tests/CMakeLists.txt`, `do.py` | — |
| D | **F5** step 1 — move `PaneDescriptor` to `draxul-types` | `libs/draxul-renderer/include/draxul/pane_descriptor.h` + include sites | gates E |

**Wave 2 — after Wave 1 (4 agents, disjoint)**

| # | Package | Files touched | Notes |
|---|---|---|---|
| E | **F5** steps 2–4 — create `draxul-shell-layout` | `app/{split_tree,chrome_layout,chrome_pill,app_shell_layout,fuzzy_match,rename_editor}.*` → new lib; `tests/CMakeLists.txt` | needs D; conflicts with `pending/01` on root CMake — sequence after it |
| F | **F3** — `draxul-control` transport split | `libs/draxul-control/` only | **both platforms required**; single owner, do not parallelize the two OS halves |
| G | **F4** steps 1–3 — `DraxulShaders.cmake` + gating | `cmake/CompileShaders*.cmake`, root `CMakeLists.txt` | conflicts with `pending/00`/`01` on root CMake — sequence after them |
| H | **F1** steps 1–2 — server registry + session store | `libs/draxul-server/src/` only | pairs with `pending/10 server-lifecycle-sigterm-eviction` |

**Wave 3 — after Wave 2**

| # | Package | Depends on |
|---|---|---|
| I | **F1** step 3 — method router + focused tests | H |
| J | **F4** step 4 — move shader files per module (SatView first, one module per commit) | G; coordinate with `pending/06` |
| K | **F6** — `app_entry` extraction | E (reduces `app/` churn first); coordinate with `ice-box/23` Phase 0 on the registration `#ifdef`s |
| L | **F10** boundary checks + **F11** contract comments | B, C, E, F, G, H — write the rules once the boundaries are final |

**Contention warning.** Four root-CMake consumers (`pending/00`, `pending/01`, E, G) all edit `CMakeLists.txt`. Land `pending/00` and `pending/01` first; they narrow the root file and make E and G nearly conflict-free.

---

## Strongest existing structural qualities — preserve these

These are unusually good for a codebase this size and several of the proposals above are deliberately shaped to reuse them rather than replace them.

1. **`cmake/CheckDependencyBoundaries.cmake` is the right mechanism.** Configure-time, specific, and its error messages teach the rule rather than just naming the edge (`:4–7`, `:15–18`). F10 extends it; nothing about it should change.

2. **The focused test-target scheme in `tests/CMakeLists.txt` is genuinely well engineered.** The partition assertion (`:103–128`), the static case/tag inventory fingerprint (`:130–181`), the `LESS`-only baselines (`:186–200`), and the "unclassified lands in core and fails to link" default (`:16–19`) together make it very hard to lose a test during a target move. Preserve all four when implementing F8.

3. **The `*-test-internals` INTERFACE pattern.** `draxul-renderer-test-internals` (`libs/draxul-renderer/CMakeLists.txt:113–119`), `draxul-megacity-test-internals`, `draxul-satview-host-test-internals`, `draxul-codeviz-renderer-test-internals` — white-box access is explicit, test-only, and does not widen any production include surface. F1 should follow it exactly rather than invent anything.

4. **`link_isolation/` build-only contracts** (`tests/CMakeLists.txt:399–432`) — eight executables each consuming one library's public header through only its owning target. A cheap, precise guard against transitive-include masking. Extend to new libraries (`draxul-shell-layout` from F5 should get one).

5. **Backend-private renderer construction.** `draxul-renderer-core`/`-metal`/`-vulkan` as OBJECT libraries with `draxul-vulkan-resources` STATIC and private linkage (`libs/draxul-renderer/CMakeLists.txt`), plus the deliberate, narrow public `draxul/vulkan/vk_render_context.h` and `draxul/metal/objc_ref.h` that product render passes consume. Verified: only backend-private TUs include them. The comment at `:60–63` explaining *why* is exactly the kind of contract F11 wants everywhere.

6. **`libs/draxul-gui` and `libs/draxul-ui`** — the model for library ownership. Named by responsibility, a README stating the contract, a dependency list justified inline, and an explicit testability promise ("must stay usable from pure unit tests without a window, GPU, or ImGui context"). This is the target state for every library in the tree.

7. **`modules/megacity/AGENTS.md`** — the best module guide here: dependency-arrow diagram, per-target ownership rules, an explicit both-backends-or-declare-the-gap rule, threading/lifetime invariants, and build-wiring obligations. Copy its structure for SatView and Score (F11); update its `draxul-tests.exe` command and `megacity_scene_tests.cpp` reference under `pending/08`, which owns that sweep.

8. **`draxul-terminal-process`'s platform split** — separate TUs and headers selected by CMake, near-zero `#ifdef`s in either. F3 asks `draxul-control` to become this.

9. **Renderer-free foundation targets.** `draxul-terminal-core`, `draxul-terminal-process`, `draxul-protocol`, `draxul-client`, and `draxul-server` are all provably free of window/renderer/host/SDL dependencies, enforced by `draxul_reject_direct_links`. That is what makes headless server testing possible at all and must survive the F1 decomposition intact.
