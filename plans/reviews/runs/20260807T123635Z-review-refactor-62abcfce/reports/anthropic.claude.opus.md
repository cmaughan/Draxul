Read the prompt and reviewed the tree directly (root/`app/`/`libs/`/`modules/`/`tests/`/`scripts/` CMake and sources, `AGENTS.md`, `CLAUDE.md`, `docs/module-map.md`, and every card in `kanban/pending`, `kanban/ice-box`, `kanban/done` to avoid re-reporting tracked work).

---

# Draxul Refactoring Review — Structure, Modularity, Testability, Agent Isolation

## Scope note

This tree already carries nine pending refactor cards (`kanban/pending/00`–`08`) covering: internal-target CMake policy, `app/CMakeLists.txt` ownership, the `draxul-host` split, the Nvim protocol/transport split, the Kanban core/host split, the MegaCity model library, the SatView composer split, the ScoreView overlay split, and agent-guidance + `do.py test --label`. Ice-box holds `22 app-tab-session-controllers`, `125 overlay-registry-refactor`, `25 renderer-backend-parity-cleanup`, and `23 core-extra-repo-split`. **None of the findings below duplicate those**; where a finding is adjacent to a card I say so explicitly and scope around it.

No P0. Nothing in the current structure blocks a safe change outright — the boundary discipline is genuinely good (see "Strengths"). The findings are high-leverage hygiene in the areas the existing cards do *not* reach: the server/control-plane libraries, process launch, the app-layer value code, the executable entry point, and test-target fan-out.

---

## F1 — `ServerKernel::Impl::handle_request` is a 976-line method inside a 3,474-line TU

**Location** `libs/draxul-server/src/server_kernel.cpp:1899-2874` (`handle_request`); class body `:339-486`; target `draxul-server` (`libs/draxul-server/CMakeLists.txt:1-9`).

**Priority** P1

**Current structural problem**
`draxul-server` already has real collaborators — `topology_service.cpp` (971), `remote_terminal_service.cpp` (880), `server_agent_service.cpp` (452), `server_terminal_runtime.cpp` (708), `session_topology_bridge.cpp` (394). Yet `server_kernel.cpp` is 3,474 lines, over half of the library's 7,082 source lines, and one method carries the entire control-method surface as a flat `if (request.method == ...)` chain:

- `:1902` `server.hello` (client registration/auth), `:1993` `server.goodbye`, `:2018-2024` status/session lifecycle
- `:2073-2075` topology routing
- `:2098-2131` the ten `agent.*` methods, then `:2205-2570` inline `agent.start` / `agent.restart` / `agent.send_text` / `agent.send_keys` bodies — ~470 lines of agent control living *next to*, not *inside*, `ServerAgentService`
- `:2574` `pane.report_agent_session`, `:2718` `pane.read`, `:2821` `server.shutdown`

The `Impl` declaration (`:339-486`) mixes five ownership domains in one class: runtime-marker publication (`publish_starting_marker`, `published_identity_matches`, `publish_failure_marker`, `remove_all_starting_markers`), session lifecycle (`initialize_session`, `delete_session`, `delete_all_sessions`, `rename_session`, `prepare_session_restore`), client registry (`register_client_hello`, `authenticate_or_touch_client`, `disconnect_client`, `prune_inactive_clients`, `forget_session_clients`), checkpointing (`checkpoint_session`, `collect_checkpoint_results`, `wait_for_checkpoint_tasks`), and terminal/agent runtime creation. There is no seam between "decode + authorize a request" and "perform the operation", so every one of the 22 method families is only reachable through the same 976-line branch.

Consequence: `libs/draxul-server` is a single-owner file. Pending `10 server-lifecycle-sigterm-eviction -bug.md` and `14 server-agent-runtime-control -feature.md` both land in it, and `09 macos-remote-terminal-channel -bug.md` names ~23 sites in its test file — three cards contending for one TU.

**Proposed boundary** No new library. Inside `draxul-server`, introduce a `ServerMethodRouter`: a `std::unordered_map<std::string_view, Handler>` built once at construction, where each handler is a small free function or a method on the *existing* service objects.

- `src/server_session_methods.{h,cpp}` — `server.hello/goodbye/status/delete_session/delete_all_sessions/rename_session/shutdown`
- `src/server_agent_methods.{h,cpp}` — the ten `agent.*` bodies, moved onto `ServerAgentService` (which already owns the projection/sanitization)
- `src/server_pane_methods.{h,cpp}` — `pane.read`, `pane.report_agent_session`
- Topology methods delegate straight to `TopologyService` (already the case at `:2073-2097`)
- `server_kernel.cpp` retains: option ownership, start/stop, marker publication, the run loop (`:3216-3527`), checkpoint scheduling, and the client registry

**Dependency shape** Public API (`include/draxul/server_kernel.h`, 101 lines) is unchanged — callers are `app/main.cpp:279` and `tests/server_kernel_tests.cpp` only. Router and method TUs are `src/`-private, depend on the existing service headers plus `draxul-protocol`/`draxul-control`. No new link edges.

**Migration path** (each step buildable)
1. Add the router with a single default arm that calls today's `handle_request`; register nothing. No behavior change.
2. Move one family at a time, lowest-risk first: `server.status`/`goodbye` → `pane.*` → `agent.*` → session lifecycle. Each move is a cut-paste plus a router entry; the fallback arm shrinks.
3. Delete the fallback when the chain is empty.

**Testing improvement** A handler that takes `(const ControlRequest&, ServerContext&)` and returns `ControlMethodResult` is directly table-testable — malformed params, wrong session, unauthenticated client, over-limit values — without starting a kernel, a control listener, or a PTY. Today all of that requires the full `ServerKernel` fixture, which is why `tests/server_kernel_tests.cpp` is 4,604 lines (the largest file in the repo).

**Agent-work benefit** Agent-control work (`pending/14`), lifecycle/eviction work (`pending/10`), and terminal-channel work (`pending/09`) become three non-overlapping files instead of three edits to one 976-line method.

**Risks / prerequisites** None cross-platform — `server_kernel.cpp` has no `#ifdef _WIN32` in the dispatcher and no Vulkan/Metal contact. The real risk is behavioral ordering: several methods depend on `authenticate_or_touch_client` (`:1537`) running first. Hoist that into the router's pre-dispatch step in step 1 and assert it once, rather than replicating it per handler.

---

## F2 — `control_plane.cpp` interleaves both platform transports, the wire format, and the client in one 1,833-line TU

**Location** `libs/draxul-control/src/control_plane.cpp`; target `draxul-control` (`libs/draxul-control/CMakeLists.txt:1-3` — one source file).

**Priority** P1

**Current structural problem**
One TU, ~15 `#ifdef _WIN32` / `#else` regions, containing four separable concerns:

| Concern | Lines | Platform |
|---|---|---|
| Framing, JSON depth/size limits, response encoding | `:192-268` (`depth_within_limit`, `frame_prefix`, `response_json`, `dump_wire_json`) | neutral |
| Metadata publication, current-user ACL/file writes, metadata cache | `:345-573` | both, heavily forked |
| `ControlServer::Impl::run` accept loop | `:1370-1529` (Win32) / `:1532-1660` (POSIX) | **two full parallel implementations** |
| `ControlClient::request` connect/write/read | `:1774-1835` (Win32) / `:1836-1891` (POSIX) | duplicated again |

Plus two parallel I/O primitive sets: `read_exact`/`write_exact` at `:603-690` and again as `client_read_exact`/`client_write_exact` at `:756-906`, each forked per platform — four near-identical byte-loop implementations differing only in deadline handling.

Evidence this is a live collision point: `kanban/done/` has *seven* separate bug cards that all edited this file (`09`, `12`, `16`, `17`, `18`, `20`, `25`, `26`), and `pending/09 macos-remote-terminal-channel -bug.md:92` already states "One owner should hold `libs/draxul-control` transport" — an explicit serialization constraint imposed by file layout, not by the problem.

**Proposed boundary** Keep one target `draxul-control`, split the sources:

- `src/control_wire.{h,cpp}` — framing, `depth_within_limit`, `dump_wire_json`, `response_json`, size/depth constants, `ControlMethodResult` helpers. **Zero platform code.**
- `src/control_endpoint_metadata.{h,cpp}` — `control_runtime_directory`, `control_metadata_path`, `namespaced_control_id`, metadata read/write/cache, current-user ACL. Platform-forked but small and cohesive.
- `src/control_transport.h` — an internal `IControlTransport` seam (listen/accept/read-frame/write-frame/close) implemented by `control_transport_win32.cpp` and `control_transport_posix.cpp`, selected in CMake, **not** by `#ifdef`.
- `src/control_server.cpp` — `Impl::start/stop/dispatch/process_pending` and the accept loop, written once against the transport seam.
- `src/control_client.cpp` — `ControlClient::request` written once against the transport seam.

**Dependency shape** Public header `include/draxul/control_plane.h` (124 lines) unchanged; consumers are `draxul-client`, `draxul-server`, `app/app.cpp`, `tests/control_plane_tests.cpp` (1,261 lines). `nlohmann_json` stays PUBLIC (it is in the public header); `advapi32` stays PRIVATE and moves to the Win32 transport TU only.

**Migration path**
1. Extract `control_wire.*` first — pure functions, no platform code, immediately unit-testable. Low risk, high payoff.
2. Extract metadata/ACL second (already mostly self-contained at `:345-573`).
3. Introduce `IControlTransport`, implement Win32 first (the CI platform), keep POSIX inline behind the old code until the Win32 path is green, then port POSIX.
4. Unify `read_exact`/`client_read_exact` last — that is where the deadline semantics from `done/17 sync-ipc-on-ui-thread` live and must be preserved verbatim.

**Testing improvement** Framing and JSON-limit tests (`kControlMaxMessageBytes`, `kControlMaxJsonDepth`, oversized-frame refusal from `done/25`, the invalid-UTF-8 crash from `done/12`) become pure-function tests with no socket, no pipe, no thread. A fake `IControlTransport` lets `control_plane_tests.cpp` exercise partial reads, torn frames, and deadline expiry deterministically on both platforms — today those paths are only reachable on the host platform.

**Agent-work benefit** A macOS socket fix and a Windows named-pipe fix become edits to different files. The "one owner holds the transport" constraint in `pending/09` can be relaxed to "one owner per transport TU".

**Risks / prerequisites** Highest-risk item in this review — this file is the singleton/eviction/liveness substrate, and `done/16 posix-server-singleton-race` shows how subtle the bind/lock ordering is. Do steps 1–2 (which carry no lifecycle logic) independently and treat step 3 as its own card. Both platforms must be built; `control_plane_tests.cpp` must pass unchanged at every step.

---

## F3 — Detached process launch is implemented three times, and the copies have already diverged

**Location**
- `libs/draxul-client/src/server_client.cpp:397` (`quote_windows_argument`), `:930-1045` (Win32 `CreateProcessW` + POSIX double-`fork`/`execv`)
- `app/server_status_surface.cpp:44` (`quote_windows_argument`, **second copy**), `:89-150` (`CreateProcessW` + `fork`/`execl`), `:453-470` (`ShellExecuteW` + `fork`)
- `app/main.cpp:598-640` (self-launch `execv`)

**Priority** P1

**Current structural problem**
Three sites implement the same contract — *launch this executable, detached, no pipes, no PTY* — with independently written platform code. `quote_windows_argument` is duplicated verbatim across a library and the app.

The divergence is not hypothetical. `server_client.cpp:978-982` carries this comment:

> "Build the argv BEFORE forking. The parent is multithreaded (GUI or test harness), and heap allocation between `fork()` and `execv()` can inherit a locked allocator and deadlock the child — the exact failure recorded in kanban done/01 macos-app-self-launch. After fork, only async-signal-safe calls run."

`app/server_status_surface.cpp:127-147` does the opposite: inside the child, after `fork()`, it constructs `std::string executable_text = executable.string()` and `runtime_text` before `execl`. The lesson was learned in one copy and not the other. This is the structural reason a fixed bug class recurs, which is exactly the case the review prompt admits as a refactoring finding.

(Distinguish from the *pipe* spawn in `libs/draxul-nvim/src/nvim_process.cpp:441-530` and the *PTY* spawns in `libs/draxul-terminal-process/src/{unix_pty_process.cpp:283, conpty_process.cpp:551}` — those have genuinely different contracts and should stay put. Only the detached family is duplicated.)

**Proposed boundary** A new narrow static library `libs/draxul-process-launch`:

```
include/draxul/process_launch.h
  struct DetachedLaunchSpec {
      std::filesystem::path executable;
      std::vector<std::string> arguments;   // argv[1..]; argv[0] is derived
      bool new_session = true;              // setsid / CREATE_NEW_PROCESS_GROUP
      bool hide_window = true;              // Win32 only, ignored elsewhere
  };
  Result<uint64_t, Error> launch_detached(const DetachedLaunchSpec&);
```

**Dependency shape** Depends only on `draxul-types` (for `Result`/`Error`, already at `libs/draxul-types/include/draxul/result.h`). Implementation TUs `src/process_launch_win32.cpp` / `src/process_launch_posix.cpp` selected in CMake; `quote_windows_argument` becomes a private detail of the Win32 TU. Consumers: `draxul-client` (PRIVATE), `draxul-app` (PRIVATE). No renderer, window, SDL, or host dependency — it satisfies the `draxul_reject_direct_links` foundation profile and belongs in the foundation row of `docs/module-map.md`.

**Migration path**
1. Create the library with the POSIX implementation copied from `server_client.cpp:977-1045` — the *correct* fork-safe version — and the Win32 implementation from `:930-976`.
2. Repoint `server_client.cpp` first (behavior-identical by construction), verify server launch on both platforms.
3. Repoint `app/server_status_surface.cpp:89-150` — this fixes the post-fork-allocation divergence as a side effect.
4. Repoint `app/main.cpp:598-640` self-launch.
5. Leave `ShellExecuteW` (`server_status_surface.cpp:453`, "open a log file") alone — different contract.

**Testing improvement** First unit-testable process launch in the repo. `tests/fd_inheritance_tests.cpp` already forks by hand at `:83` and `:140` to probe descriptor inheritance; those assertions can target `launch_detached` instead. Argument-quoting is a pure function with a table test (currently untested in either copy). Today `server_client.cpp`'s launch path is only exercised through a live server start.

**Agent-work benefit** Process-spawn semantics stop being three separate expert areas. An agent fixing launch behavior touches one library with one test file, instead of grepping for `CreateProcessW` across `libs/` and `app/`.

**Risks / prerequisites** macOS `fork()` async-signal-safety must be preserved exactly — port the `server_client.cpp` version, never the `server_status_surface.cpp` version. Windows flag differences are real and must be parameters, not hard-coded: `server_client.cpp:964` uses `CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP`, `server_status_surface.cpp:110` uses only `CREATE_NEW_PROCESS_GROUP`. Verify server-launch-from-GUI, server-launch-from-tray, and self-relaunch on both platforms.

---

## F4 — Dependency-free layout/matching code is trapped inside `draxul-app`, so its tests link the entire product closure

**Location** `app/split_tree.{h,cpp}` (823 lines), `app/chrome_layout.{h,cpp}` (875), `app/rename_editor.{h,cpp}` (248), `app/chrome_pill.{h,cpp}` (150), `app/app_shell_layout.{h,cpp}` (134), `app/fuzzy_match.{h,cpp}` (136), `app/frame_timer.h` (35), `app/session_id.{h,cpp}` (109), `app/weather_service.{h,cpp}` (367). Target `draxul-app` (`CMakeLists.txt:281-341`); tests in `draxul-test-app` (`tests/CMakeLists.txt:274-292`).

**Priority** P1

**Current structural problem**
I checked every include of this cluster. It is pure:

| File | Non-std includes |
|---|---|
| `fuzzy_match.h/.cpp` | *(none)* |
| `app_shell_layout.h/.cpp` | *(none)* |
| `frame_timer.h` | *(none)* |
| `split_tree.h/.cpp` | `draxul/pane_descriptor.h`, `draxul/session_model.h`, `draxul/log.h`, `draxul/perf_timing.h` |
| `rename_editor.h/.cpp` | `split_tree.h` |
| `chrome_pill.h/.cpp` | `draxul/chrome_theme.h` |
| `chrome_layout.h/.cpp` | `app_shell_layout.h`, `chrome_pill.h`, `rename_editor.h`, `draxul/app_config_types.h`, `draxul/system_resource_monitor.h`, `draxul/unicode.h` |
| `session_id.h/.cpp` | `draxul/result.h` |
| `weather_service.h/.cpp` | `draxul/http/http_client.h` |

No window, renderer, font, grid, host, SDL, or ImGui. Yet because they live in `draxul-app`, the tests that cover them — `split_tree_tests.cpp`, `chrome_layout_tests.cpp`, `app_shell_layout_tests.cpp`, `fuzzy_match_tests.cpp`, `frame_timer_tests.cpp`, `pane_status_pill_tests.cpp`, `http_weather_tests.cpp` — are in `draxul-test-app`, which links `draxul-app` (PUBLIC → renderer, ui, font, grid, gui, nvim, host, nanovg, client, http, config, runtime-support, render-test), plus `draxul-markdown-host`, `draxul-kanban`, and, when enabled, `draxul-megacity`, `draxul-satview-host`, `draxul-scoreview-host` — which transitively drags in Vulkan/Metal, ImGui, SDL3, Verovio, RtMidi, TinySoundFont, KissFFT.

A one-line edit to `fuzzy_match.cpp` relinks against Verovio. Separately, `weather_service` is HTTP product behavior sitting in a directory `docs/module-map.md:60` declares "top-level orchestration only".

**Proposed boundary** One new static library `libs/draxul-shell-layout` (~2,500 lines moved, zero rewritten), holding the split tree, chrome/tab-bar layout math, shell layout, rename editor state, pill formatting, fuzzy matching, frame timing, and session-id parsing. `weather_service` moves separately to `libs/draxul-http` or a sibling `libs/draxul-weather` — it is the one member with an outbound network dependency and should not be bundled with layout.

**Dependency shape**
- Depends on: `draxul-types`, `draxul-session-model`, `draxul-config` (for `app_config_types.h`), `draxul-runtime-support` (for `SystemResourceMonitor`), `draxul-performance`.
- Must **not** depend on: `draxul-host`, `draxul-renderer`, `draxul-window`, `draxul-font`, SDL3 — enforceable with `draxul_reject_direct_links` (see F7).
- Callers: `draxul-app` only (`pane_manager.cpp`, `tab_controller.cpp`, `chrome_host.cpp`, `command_palette.cpp`, `app.cpp`).
- Deliberately excluded: `render_tree.{h,cpp}` — `render_tree.cpp` includes `draxul/host.h`. Revisit after `pending/02` lands `draxul-host-api`.

**Migration path**
1. Create the library with the four zero-dependency files (`fuzzy_match`, `app_shell_layout`, `frame_timer`, `session_id`) and repoint `draxul-app`. Trivial, proves the target.
2. Move `split_tree` + `rename_editor`.
3. Move `chrome_pill` + `chrome_layout`.
4. Move `weather_service` to its own home.
5. Add `draxul-test-shell-layout` (CTest label `shell`) and migrate the seven test files out of `DRAXUL_APP_TEST_SOURCES` (`tests/CMakeLists.txt:25-59`). The configure-time partition check at `:116-128` and the registration/tag baselines at `:186-190` will prove no case was lost.

**Testing improvement** Seven test files (~2,000 lines) move from a 2-shard executable linking six product libraries to a target linking five foundation libraries. Split-tree geometry, tab-bar hit-testing, and fuzzy ranking become buildable in seconds with no GPU stack. This also unblocks ice-box `08 splittree-min-pane-size -test.md`, `12 cli-args-cwd-behavior -test.md`, and `34 chromehost-tab-geometry-dpi -test.md`, all of which currently require the full app closure.

**Agent-work benefit** Directly complements ice-box `22 app-tab-session-controllers` (which needs exclusive ownership of `app/app.cpp`): with the value code out of the way, `app/` shrinks to genuine orchestration, and layout work stops colliding with session/tab work.

**Risks / prerequisites** Mechanical, no platform risk (nothing in the cluster is `#ifdef`-forked). Sequence *after* `pending/01 app-local-cmake-ownership` so `app/CMakeLists.txt` exists and the source-list edit happens in one place. Do not fold `render_tree` in early.

---

## F5 — `app/main.cpp` (1,174 lines) is unreachable from every test target, and its provider-registration ladder is duplicated in `tests/test_main.cpp` with a behavioral divergence

**Location** `app/main.cpp`; `CMakeLists.txt:349-352` (`add_executable(draxul app/main.cpp ...)`); `tests/test_main.cpp:55-75`.

**Priority** P1

**Current structural problem**
`main.cpp` is compiled *only* into the `draxul` executable — not into `draxul-app`. Nothing in it is reachable by any of the seven test executables. It contains far more than a `main`:

- `:67-155` Windows console/standard-handle bootstrap and UTF-16 argv conversion
- `:197-225` signal/console-control handlers
- `:237-~540` `run_server_mode` — including the `AppConfig` → `std::vector<AgentDefinition>` adapter at `:259-278` and the full `ServerKernelOptions` assembly at `:279-294`
- `:598-640` macOS self-launch (`execv`)
- `:652-671` host provider registration
- `:931-1104` render-test and screenshot mode wiring

The registration ladder at `main.cpp:652-671` is reproduced line-for-line at `tests/test_main.cpp:59-74` — and the copies already disagree: `main.cpp:655-659` gates `register_server_shell_host_metadata` on `shared_server`, while `test_main.cpp:62` registers it unconditionally. So `tests/host_provider_availability_tests.cpp` validates a registry the application never actually builds in the non-shared-server case.

**Proposed boundary** Move every non-`main` body into `draxul-app` (or a thin `app/entry/` group inside it), leaving `app/main.cpp` as a ~40-line shim: platform console setup → `draxul::app_entry::run(args)`.

- `app/app_entry.{h,cpp}` — mode dispatch (`--server`, `--server-stop-dialog`, `--render-test`, `--smoke-test`, `--gui-action`, normal GUI)
- `app/server_mode.{h,cpp}` — `run_server_mode`, signal handlers, and the config→`ServerKernelOptions` adapter as a **pure free function** `ServerKernelOptions make_server_kernel_options(const AppConfig&, const ParsedArgs&, path runtime_dir)`
- `app/host_providers.{h,cpp}` — a single `register_product_host_providers(HostProviderRegistry&, bool shared_server)` called by both `main.cpp` and `tests/test_main.cpp`

**Dependency shape** No new targets and no new link edges — these files join the existing `draxul-app` source list at `CMakeLists.txt:282-313`. The executable keeps `draxul.rc`, the macOS bundle/icon rules, and the optional product links (`CMakeLists.txt:381-392`), preserving the "products link only at the executable boundary" invariant.

**Migration path** Extract in this order, each independently verifiable: (1) `register_product_host_providers` and repoint both callers — this alone removes the divergence; (2) `make_server_kernel_options` as a pure function; (3) `run_server_mode`; (4) mode dispatch. Windows console setup and `WinMain`/`main` stay in `main.cpp`.

**Testing improvement** `make_server_kernel_options` becomes table-testable (agent-profile mapping, restore-policy fallback at `main.cpp:271-277`, scrollback/shell defaults) — currently zero coverage. Mode dispatch becomes testable without launching a process. `host_provider_availability_tests.cpp` starts validating the registry the app actually builds.

**Agent-work benefit** This is a genuine hotspot: sixteen kanban cards reference `app/main.cpp` by line number. Splitting it by mode gives server work, CLI work, and render-test work three separate files.

**Risks / prerequisites** Adjacent to ice-box `23 core-extra-repo-split -architecture.md:28-33`, whose Phase 0 wants the same registration block replaced with a single seam for a different reason (splitting the repo). **Coordinate — do not do it twice.** The seam proposed here (`register_product_host_providers`) is the natural place for that card's `DRAXUL_HAVE_EXTRA_HOSTS` guard, so doing this first makes that card cheaper. Also overlaps `pending/10 server-lifecycle-sigterm-eviction -bug.md:71-72`, which edits the signal handling in `main.cpp`; sequence after it or hand both to one owner. Windows `WinMain` vs POSIX `main` and the macOS bundle self-launch path must both be exercised.

---

## F6 — `draxul-test-core` is a 97-source catch-all linking 17 libraries; the server/session cluster needs its own target, and two libraries still lack `*-test-internals` seams

**Location** `tests/CMakeLists.txt:110-111` (core = everything unclassified), `:247-272` (core links `draxul-bmp`, `draxul-performance`, `draxul-app-support`, `draxul-http`, `draxul-window`, `draxul-renderer`, `draxul-ui`, `draxul-font`, `draxul-grid`, `draxul-nvim`, `draxul-config`, `draxul-client`, `draxul-protocol`, `draxul-server`, `draxul-gui`, `draxul-runtime-support`, `draxul-render-test`, `draxul-host`, `draxul-nanovg`, `draxul-renderer-test-internals`). Offending includes: `tests/server_kernel_tests.cpp:3-7`, `tests/font_tests.cpp:6-7`, `tests/font_resolver_style_tests.cpp:5-7`, `tests/glyph_raster_error_tests.cpp:3-6`, `tests/box_drawing_tests.cpp:5`, `tests/font_style_model_tests.cpp:3`.

**Priority** P1

**Current structural problem**
`kanban/done/35 modular-test-targets` split the monolith into seven executables, but "core" absorbed everything unclassified: 97 files, ~30,000 lines, spanning fonts, grid, VT, RPC, renderer, and the whole server/client/protocol stack. Two consequences:

*(a) Server/session tests carry a GPU/font closure they never use.* The cluster — `server_kernel_tests.cpp` (4,604), `server_protocol_tests.cpp` (523), `client_recovery_tests.cpp` (333), `agent_protocol_tests.cpp` (212), `agent_discovery_tests.cpp` (177), `topology_projection_tests.cpp` (171) — needs only `draxul-server`, `draxul-client`, `draxul-protocol`, `draxul-control`, `draxul-session-model`, `draxul-agent`. I verified their includes: not one references a renderer, window, or font header. Yet touching `server_kernel.cpp` relinks a binary containing Vulkan, ImGui, SDL3, FreeType, and HarfBuzz.

*(b) The `*-test-internals` pattern is incomplete and unenforced.* `done/35` recorded that "test targets no longer name any library/module `src/` directory". Six files still do, via repository-relative paths:

```
tests/server_kernel_tests.cpp:3   #include "../libs/draxul-server/src/fake_terminal_runtime.h"
tests/server_kernel_tests.cpp:4-7  ... remote_terminal_service.h, server_terminal_runtime.h,
                                       session_topology_bridge.h, topology_service.h
tests/font_tests.cpp:6-7           #include "../libs/draxul-font/src/font_resolver.h", font_selector.h
tests/font_resolver_style_tests.cpp:5-7, glyph_raster_error_tests.cpp:3-6,
tests/box_drawing_tests.cpp:5, font_style_model_tests.cpp:3
```

`draxul-renderer`, `draxul-megacity`, `draxul-codeviz-renderer`, `draxul-satview-host`, `draxul-satview-renderer`, and `draxul-scoreview-host` all publish a proper `*-test-internals` INTERFACE target. `draxul-server` and `draxul-font` do not, so those tests hard-code the source-tree layout — which silently blocks the very file moves F1 proposes.

**Proposed boundary**
1. `add_library(draxul-server-test-internals INTERFACE)` in `libs/draxul-server/CMakeLists.txt` and `draxul-font-test-internals` in `libs/draxul-font/CMakeLists.txt`, each exporting `src/` and linking its owner — exactly the shape at `libs/draxul-renderer/CMakeLists.txt:124-128`.
2. New `DRAXUL_SERVER_TEST_SOURCES` list and `draxul_add_test_target(draxul-test-server "server;session" 2 ...)` linking only `draxul-server-test-internals`, `draxul-client`, `draxul-protocol`, `draxul-control`, `draxul-session-model`, `draxul-agent`.
3. A configure-time guard rejecting `#include "../libs` / `#include "../modules` in any test source — the same style as the existing partition assertion at `tests/CMakeLists.txt:106-128`.

**Dependency shape** No production link edges change. `draxul-test-core` loses six sources and (once the server/client/protocol libs are dropped from its list) three link edges.

**Migration path** Add the two internals targets and repoint the six includes (mechanical, no behavior change) → add `draxul-test-server` and move the six sources → drop `draxul-server`/`draxul-client`/`draxul-protocol` from `draxul-test-core`'s link list if nothing else needs them → add the include guard. The existing partition/registration/tag baselines (`tests/CMakeLists.txt:116-204`) prove no case was lost at every step.

**Testing improvement** Enables `tests/server_kernel_tests.cpp` to be split along F1's handler families later. Gives `pending/09`, `10`, and `14` a focused build/run target.

**Agent-work benefit** Adjacent to but not overlapping `pending/02` and `pending/03`, which re-link *terminal-core* and *nvim* tests as part of their library splits; neither touches the server/session bucket or the missing internals targets. `pending/08` adds `do.py test --label` for existing labels — a new `server` label plugs straight into it.

**Risks / prerequisites** `server_kernel_tests.cpp` has platform-forked sections (`windows.h`/`tlhelp32.h` vs `csignal`/`unistd.h`, `:20-28`); both must still compile. Sequence the internals targets *before* F1 so the file moves don't break test includes.

---

## F7 — `CheckDependencyBoundaries.cmake` guards only ten foundation targets; the app↔product and product↔product invariants are comments, not checks

**Location** `cmake/CheckDependencyBoundaries.cmake:21-93`; invariant documented only in prose at `CMakeLists.txt:342-344` and `docs/module-map.md:52-54`.

**Priority** P2

**Current structural problem**
The checker is a genuine strength — `draxul_reject_direct_links` at `:43-69` and the product-leak loop at `:71-92` catch real regressions at configure time. But its coverage is narrow:

- The loop covers ten foundation targets. `draxul-config`, `draxul-agent`, `draxul-window`, `draxul-font`, `draxul-grid`, `draxul-gui`, `draxul-ui`, `draxul-renderer`, `draxul-nanovg`, `draxul-runtime-support` have no rules at all.
- Nothing checks the *reverse* direction between product modules: `draxul-satview-host` linking `draxul-megacity` would configure cleanly today. (I verified no such edge currently exists — every module CMakeLists links only downward into `libs/`. The invariant holds by discipline alone.)
- The headline invariant — "`draxul-app` deliberately has no source-level dependency on optional product modules" (`CMakeLists.txt:342-344`) — is enforced by a code comment. An agent adding `target_link_libraries(draxul-app PUBLIC draxul-megacity)` gets a green build.
- `draxul-app-support` (`libs/draxul-app-support/CMakeLists.txt`) is an INTERFACE target whose only content is three link edges (`draxul-config`, `draxul-runtime-support`, `draxul-render-test`). It exists to widen fan-out in one line, and it is unchecked.

**Proposed boundary** Extend the existing function — no new file, no new tooling:

```cmake
draxul_reject_direct_links(draxul-app
    draxul-megacity draxul-satview-host draxul-scoreview-host)
foreach(_product IN ITEMS draxul-megacity draxul-satview-host
                          draxul-scoreview-host draxul-markdown-host draxul-kanban)
    # reject links to every *other* product family
endforeach()
draxul_reject_direct_links(draxul-config draxul-host draxul-renderer draxul-window SDL3::SDL3)
```
Plus, once F4 lands, `draxul_reject_direct_links(draxul-shell-layout draxul-host draxul-renderer draxul-window draxul-font SDL3::SDL3)`.

**Dependency shape** Configure-time only. Must be guarded by `if(TARGET ...)` for the optional products so `DRAXUL_ENABLE_*=OFF` still configures.

**Migration path** One commit; all rules pass on the current tree, so it is a pure ratchet with no code changes.

**Testing improvement** Turns three prose invariants (`CMakeLists.txt:342`, `module-map.md:52-54`, `modules/megacity/AGENTS.md:7`) into configure-time failures with actionable messages.

**Agent-work benefit** Highest guidance-value-per-line change in this review. An agent that does not read `docs/module-map.md` gets told, at configure time, exactly which edge it must not add.

**Risks / prerequisites** Depends on `pending/00 internal-target-build-policy`, which is reworking configure-time target auditing — the two should share one pass over the target list. Verify with all five optional-product combinations.

---

## F8 — ScoreView core and host share one PUBLIC `include/` directory, so the split is unenforceable

**Location** `modules/score/draxul-scoreview/CMakeLists.txt:19-21` and `:68-70` — both `draxul-scoreview` and `draxul-scoreview-host` declare `target_include_directories(... PUBLIC include)` on the *same* directory.

**Priority** P2

**Current structural problem**
Two libraries with genuinely different dependency profiles publish the same header tree. Host-owned headers therefore appear on the include path of every core-only consumer:

| Header (in the shared `include/draxul/scoreview/`) | Implemented in |
|---|---|
| `score_host.h` (362 lines) | `draxul-scoreview-host` (`src/score_host.cpp`, 2,065) |
| `analysis_overlay.h` | `src/analysis_overlay.cpp` (host) |
| `score_render_nvg.h`, `keyboard_render_nvg.h` | host (NanoVG) |
| `player_input_rig.h`, `mic_player_input.h` | host (SDL/AVFoundation) |

The CMake comment at `:1-5` states the intended three-layer split precisely — the include layout just doesn't express it. A core-only test target (`draxul-test-scoreview`, `tests/CMakeLists.txt:324-325`) can `#include <draxul/scoreview/score_host.h>` and only discovers the mistake at link time, if at all. Note the *host* target correctly keeps its true internals private via `draxul-scoreview-host-test-internals` (`:96-102`) — the gap is specifically the shared public tree. This also makes the `draxul-scoreview` public surface look larger than it is, which matters for ice-box `23 core-extra-repo-split`'s inter-repo contract.

**Proposed boundary** Split the include roots: `include/` stays with `draxul-scoreview`; host-owned headers move to a new `host_include/draxul/scoreview/` published only by `draxul-scoreview-host`. Include paths (`<draxul/scoreview/score_host.h>`) are unchanged for every consumer — `app/main.cpp:38`, `tests/test_main.cpp:19` — so this is source-compatible.

**Dependency shape** No link-edge change. `draxul-scoreview-host` already PUBLIC-links `draxul-scoreview`, so its consumers still see both trees.

**Migration path** `git mv` six headers, add `host_include` to the host target, build. Single commit.

**Testing improvement** Makes `pending/07 scoreview-analysis-overlay-boundary` verifiable: that card moves overlay *construction* into `draxul-scoreview` and keeps NanoVG replay host-private, and its acceptance criterion "pure overlay tests link to `draxul-scoreview`, not `draxul-scoreview-host`" is only checkable once the include trees are separated. `analysis_overlay.h` is the header both cards touch.

**Agent-work benefit** Sequence this as step 0 of `pending/07`, same owner. Not worth a separate card.

**Risks / prerequisites** ScoreView is `DRAXUL_ENABLE_SCOREVIEW`-gated and defaults ON for Windows/macOS (`CMakeLists.txt:35-40`); verify ON and OFF. The macOS `mic_permission.mm`/AVFoundation wiring (`:85-92`) must not move.

---

## F9 — Two large unowned artifacts sit in the repo with no generator, refresh rule, or guidance entry

**Location** `REPO_STATE.md` (1,975 lines, 73 KB, repo root); `db/coverage.lcov` (805 KB, tracked).

**Priority** P2

**Current structural problem**

*`REPO_STATE.md`* — sits at the repo root beside `CLAUDE.md`, `AGENTS.md`, `GEMINI.md`, and `README.md`. Its sections are `## Branch`, `## HEAD`, `## Status`, `## Unstaged diff` (lines 37–1981), `## Staged diff`. It is a frozen snapshot of one working tree: a branch name, a SHA, a list of modified/deleted files, and ~1,900 lines of diff. I found **no producer** — no reference in `do.py` or anywhere under `scripts/` — and **no mention** in `CLAUDE.md`, `AGENTS.md`, or `docs/module-map.md`. It is stale by construction: it also embeds a verbatim copy of `do.py`'s help text (`REPO_STATE.md:711`, `:782`), so it will silently contradict `do.py` after any CLI change. An agent orienting from the repo root has no way to know it is not authoritative — and `CLAUDE.md` is explicit that shared rules live in exactly one place.

*`db/coverage.lcov`* — a legitimate runtime asset (the MegaCity `LCOV Coverage` view reads it; `docs/features.md:204`), but it is an 805 KB machine-generated blob rewritten wholesale by `do.py coverage` (`do.py:1223`). It is not in `.gitignore`, which ignores only `db/*.sqlite3-shm`/`-wal`. Any agent that runs `do.py coverage` produces an 805 KB unreviewable diff that conflicts with every other such run. There is no documented policy for when it should be refreshed and committed.

**Proposed boundary**
- `REPO_STATE.md`: delete it, or (if some workflow depends on it) move it to `plans/` — beside the already-gitignored `plans/reviews/all_code.md` and `plans/prompts/history/` — add the generating command to `do.py`, and add it to `.gitignore`. Either way, one line in `CLAUDE.md` saying what it is and that it is not authoritative.
- `db/coverage.lcov`: keep tracked (the feature needs a checked-in default), but document the refresh policy in `CLAUDE.md` — "regenerated only by an explicit coverage-refresh task; do not commit incidental regenerations" — and teach `do.py hygiene` (`do.py:951`, which already checks for forbidden tracked artifacts and duplicate feature docs) to flag an unintended modification.

**Dependency shape** Documentation and tooling only.

**Migration path** One commit. Add a `do.py hygiene` assertion so it cannot regress.

**Testing improvement** `tests/do_py_tests` already exists as a CTest entry (`tests/CMakeLists.txt:356-366`); a hygiene assertion for these two artifacts fits there directly.

**Agent-work benefit** Removes a stale, prominent, unowned source of truth from the root, and removes an 805 KB merge-conflict generator from routine coverage work.

**Risks / prerequisites** Confirm with the owner that no external workflow consumes `REPO_STATE.md` before deleting. Adjacent to `kanban/done/34 repository-hygiene-doc-source -refactor.md`, which established the `do.py hygiene` check and the root-`FEATURES.md`-as-pointer convention — this is the same discipline applied to two artifacts that escaped it. The *content* of `modules/megacity/AGENTS.md:71` (`draxul-tests.exe`) and `do.py:1063` ("four C++ shards") is stale for the same reason, but that is already owned by `pending/08 agent-guidance-label-validation` — not re-reported here.

---

## Proposed module / target map

Only the deltas from today's graph are shown; everything else is unchanged.

```
NEW  libs/draxul-process-launch          [F3]  foundation row
     public:  process_launch.h → launch_detached(DetachedLaunchSpec)
     deps:    draxul-types
     private: process_launch_win32.cpp | process_launch_posix.cpp
     callers: draxul-client (PRIVATE), draxul-app (PRIVATE)

NEW  libs/draxul-shell-layout            [F4]  between foundations and app
     public:  split_tree.h, chrome_layout.h, chrome_pill.h, rename_editor.h,
              app_shell_layout.h, fuzzy_match.h, frame_timer.h, session_id.h
     deps:    draxul-types, draxul-session-model, draxul-config,
              draxul-runtime-support, draxul-performance
     forbidden: draxul-host, draxul-renderer, draxul-window, draxul-font, SDL3
     callers: draxul-app only

MOVE app/weather_service.*  →  libs/draxul-http (or libs/draxul-weather)   [F4]

SPLIT libs/draxul-control (same target, new TUs)                            [F2]
     control_wire.cpp                  platform-free framing/JSON policy
     control_endpoint_metadata.cpp     metadata + ACL + cache
     control_transport_{win32,posix}.cpp  behind internal IControlTransport
     control_server.cpp / control_client.cpp   written once each

SPLIT libs/draxul-server (same target, new TUs)                             [F1]
     server_method_router.cpp
     server_session_methods.cpp | server_agent_methods.cpp | server_pane_methods.cpp
     server_kernel.cpp → options, start/stop, run loop, markers, checkpoints

NEW  draxul-server-test-internals, draxul-font-test-internals (INTERFACE)   [F6]
NEW  draxul-test-server  (CTest label: server;session)                      [F6]
NEW  draxul-test-shell-layout (CTest label: shell)                          [F4]

MOVE app/main.cpp bodies → draxul-app: app_entry.*, server_mode.*,
     host_providers.*  (main.cpp becomes a ~40-line shim)                   [F5]

SPLIT modules/score/draxul-scoreview: include/ (core) | host_include/ (host) [F8]
EXTEND cmake/CheckDependencyBoundaries.cmake with app↔product and
     product↔product reject rules                                           [F7]
```

---

## Best isolated work packages for parallel agents

Ordered by leverage. Packages **A–D** are fully disjoint in files and can run concurrently today.

| # | Package | Files touched | Blocks / blocked by |
|---|---|---|---|
| **A** | **F3** — `draxul-process-launch` + repoint three call sites | `libs/draxul-process-launch/*` (new), `server_client.cpp`, `server_status_surface.cpp`, `main.cpp:598-640` | Independent. Best first pick: new library, three surgical edits, fixes a recurring bug class. |
| **B** | **F6a** — the two `*-test-internals` targets + repoint six includes | `libs/draxul-server/CMakeLists.txt`, `libs/draxul-font/CMakeLists.txt`, six `tests/*.cpp` header lines | Independent; **prerequisite for F1**. ~30 minutes, unblocks a large refactor. |
| **C** | **F7** — extend the boundary checker | `cmake/CheckDependencyBoundaries.cmake` only | Coordinate with `pending/00`. All rules already pass — pure ratchet. |
| **D** | **F9** — `REPO_STATE.md` + `db/coverage.lcov` policy | root, `.gitignore`, `CLAUDE.md`, `do.py hygiene` | Independent. Needs one owner decision on `REPO_STATE.md`. |
| **E** | **F1** — ServerKernel method router (incremental, one family per commit) | `libs/draxul-server/src/*` | After **B**. Serializes with `pending/09`, `10`, `14` — assign to whoever owns those. |
| **F** | **F4** — `draxul-shell-layout` + `draxul-test-shell-layout` | `app/*` (9 file pairs), `tests/CMakeLists.txt` | After `pending/01`. Touches `app/CMakeLists.txt` — serializes with ice-box `22`. |
| **G** | **F2 steps 1–2** — `control_wire` + endpoint metadata extraction | `libs/draxul-control/src/*` | Independent of A–F. **Stop before the transport seam** (step 3 deserves its own card and its own risk budget). |
| **H** | **F5** — `main.cpp` decomposition | `app/main.cpp`, `tests/test_main.cpp`, `app/` | After `pending/10`. Coordinate the registration seam with ice-box `23` Phase 0. |
| **I** | **F8** — ScoreView include-tree split | `modules/score/draxul-scoreview/` | Fold into `pending/07` as its step 0, same owner. |

**Do not parallelize:** F2 step 3 (transport seam) with anything else in `draxul-control`; F1 with `pending/09`/`10`/`14`; F4 with ice-box `22`.

---

## Strongest existing structural qualities — preserve these

1. **Configure-time dependency-boundary enforcement.** `cmake/CheckDependencyBoundaries.cmake` with `draxul_check_direct_link` / `draxul_reject_direct_links` plus the product-leak loop (`:71-92`) is the single best structural asset here. It turns architecture into build errors. F7 extends it rather than replacing it.

2. **Genuine renderer backend isolation.** `libs/draxul-renderer/CMakeLists.txt` builds `draxul-renderer-core` (OBJECT), one backend OBJECT library per platform, and a separate `draxul-vulkan-resources` STATIC target consumed *privately* by `draxul-markdown-host`, `draxul-codeviz-renderer`, and `draxul-satview-renderer` — no Vulkan or Metal type leaves the public include tree. Product renderers reuse GPU helpers without inheriting the backend API.

3. **Optional products link only at the executable boundary.** `draxul-app` has zero product edges (`CMakeLists.txt:322-344`); MegaCity, SatView, and ScoreView attach at `add_executable` (`:381-392`). Every module CMakeLists I checked links only downward into `libs/` — no product-to-product edge exists.

4. **The `*-test-internals` INTERFACE pattern.** Renderer, MegaCity, CodeViz renderer, SatView host/renderer, and ScoreView host each publish a named white-box seam instead of teaching tests the source layout. F6 completes it for the two libraries that were missed.

5. **Self-verifying test classification.** `tests/CMakeLists.txt:106-204` proves at configure time that the focused targets partition every `*_tests.cpp` exactly once, then statically scans `TEST_CASE` registrations and tag tokens against a recorded baseline — including cases gated behind another platform or a disabled product. That is how the F4/F6 test-target moves can be made safely.

6. **Manifest-driven render scenarios.** `CMakeLists.txt:580-655` treats `tests/render/manifest.json` as the authority and fails configuration on an unregistered scenario file, a missing required reference, or a duplicate name. Generated artifacts cannot drift from their registry.

7. **Narrow, well-named foundation targets.** `draxul-types` (388 source lines, mostly header-only values), `draxul-performance`, `draxul-bmp`, `draxul-host-identity` — the module map explains *why* each was kept out of `draxul-types` (`docs/module-map.md:92-95`). The reasoning is recorded in the tree, not just in review notes.

8. **Comment-level rationale at the boundaries that matter.** `server_client.cpp:978-982` (fork safety), `CMakeLists.txt:342-344` (megacity non-linkage), `modules/score/draxul-scoreview/CMakeLists.txt:45-52` (why each dep is PUBLIC), `libs/draxul-nanovg/CMakeLists.txt` (`NVG_NO_STB` vs MegaCity symbol clash). This is what lets a bounded agent task stay bounded — F7 and F9 exist to move a few of the remaining prose invariants into enforced ones.
