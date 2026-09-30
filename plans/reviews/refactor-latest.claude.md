# Draxul structural refactoring review: hygiene, modularity, testability and parallel agent work

**Scope and method.** The only input was `repomix-output.xml` (1,700 embedded files, about 421k lines). I read the root `CLAUDE.md`, `AGENTS.md` and `GEMINI.md`, `docs/module-map.md`, the root, `tests/` and `cmake/` build files, `do.py`, the CI workflow, and every root and product Kanban lane. Four read-only subagents each reviewed one subsystem:

- **A:** the app layer and configuration.
- **B:** client, server, control, session, terminal, PTY, Neovim and hosts.
- **C:** rendering, fonts, SDK, plugin support, and the Markdown and Kanban modules.
- **D:** the five product plugins.

All four finished. Before accepting a finding I re-read the key evidence for it in the pack (CMake link lines, `#include` lines, and the duplicated code), and I merged findings that overlapped.

**Limitations:**
- The subagents ran on the same model family (Opus), but the tool doesn't let me set their reasoning effort explicitly.
- Coverage is substantial but not exhaustive; the table at the end lists the gaps.
- Line numbers are the embedded source line numbers.

**Excluded as already tracked:** SatView ice-box cards 40, 41 and 42, ScoreView ice-box cards 18 and 19, and ScoreView done card 05. `plans/shared-library-consolidation.md` records slices 1–7 as landed. That plan's topology↔layout conversion, `full_grid_update`, `TerminalSurfaceHostBase`, `process_util`, ImGui core, adapter shell and `VulkanResources` work is done, so none of it is re-proposed here. The root `kanban/pending` and `ice-box` lanes are empty.

---

## Findings (ranked by leverage and sequencing)

### 1. P0: The core test graph is a monolith and depends on the whole app plus every product
- **Location:**
  - `tests/CMakeLists.txt:301-330` (`draxul-test-core` links window, renderer, ui, font, grid, config, client, protocol, server, gui, runtime-support, render-test, all host leaves, nanovg and CameraInput) and `:820-822`.
  - Root `CMakeLists.txt:295-297`: `add_dependencies(draxul-test-core draxul)`.
  - `cmake/DraxulPlugins.cmake:356-360`: `draxul` depends on every product staging target.
  - `CLAUDE.md:265-270`.
- **Problem:**
  - About 93 unclassified `*_tests.cpp` files (roughly 37k lines) land in this one executable. They include pure protocol, control, server, client, VT/terminal-core, PTY, font, renderer-state and render-harness suites. Examples: `server_protocol_tests`, `control_transport_tests`, `server_session_checkpoint_tests`, `terminal_vt_tests`, the `vt_parser_*` suites, `grid_tests`, and roughly 12 `font_*` suites.
  - Only the host-layer suites need renderer or window fakes.
  - Only two files use `DRAXUL_EXECUTABLE_PATH` (`server_process_integration_tests.cpp:9-25` and `server_topology_terminal_tests.cpp:13,1649`). Even so, the target-level dependency on `draxul` means the documented narrow loop `do.py test debug --target draxul-test-core --catch "[server]"` builds the app and every enabled product plugin.
  - The comment at `:331-333` ("Four core suites launch this helper") is stale; only the nvim-transport suites use rpc-fake.
- **Proposed boundary:** focused targets, each with the smallest link set:

  | Target | Links |
  |---|---|
  | `draxul-test-terminal-core` | terminal-core, grid |
  | `draxul-test-protocol` | protocol |
  | `draxul-test-control` | control-test-internals |
  | `draxul-test-server-client` | server, client, and their test-internals |
  | `draxul-test-process` | terminal-process-test-internals |
  | `draxul-test-font` | font-test-internals |
  | `draxul-test-renderer` | renderer-test-internals |
  | `draxul-test-terminal-host` | terminal-host plus fakes |
  | `draxul-test-server-integration` (label `integration`) | the only target that depends on `draxul` |

- **Migration:**
  1. Split the CLI-driven cases out of `server_topology_terminal_tests.cpp`.
  2. Move the `add_dependencies(... draxul)` line to the integration target.
  3. Add one source list per target. The existing partition, duplication and inventory tripwires (`tests/CMakeLists.txt:111-259`) prove that no case is lost.
  4. Apply `draxul_reject_transitive_links(<t> draxul-renderer draxul-window draxul-font)` to the non-host targets, following the existing pattern at `:412-413`.
- **Testing and agent benefit:** A server, protocol or VT change stops relinking the GPU and font stack and stops building products. Agents working in different modules get independent binaries and meaningful CTest labels.
- **Risks:** `conpty_process_tests` and `fd_inheritance_tests` are platform-gated, so keep them in the process target on both OSes. Findings 2 and 23 need updating in the same change.

### 2. P0: Test-scope selection is a hand-maintained registry that has drifted from CMake
- **Location:**
  - `do.py:1001-1045` (hardcoded CTest regexes).
  - `tests/CMakeLists.txt:711-749` and `:787-818` (aggregates).
  - `plugins/megacity/cmake/Tests.cmake:19-22` and `:36-38`.
  - `tests/do_py_tests.py:796-804`.
  - `.github/workflows/build.yml:60-61,108-109` together with `scripts/run_tests.sh:125-129`.
- **Problem:**
  - `draxul-tests-core` builds `draxul-test-app-shell` and `draxul-test-host-api` (`tests/CMakeLists.txt:716-717`). However, the default `do.py test` regex (`do.py:1002-1019`) omits both, so the documented completed-slice gate never runs them.
  - `draxul-test-megacity-model` (MegaCity `Tests.cmake:19-20`) is in neither the `draxul-tests-megacity` aggregate (`tests/CMakeLists.txt:787-791`) nor the `--megacity` regex (`do.py:1024-1026`), so `do test --megacity` never builds or runs it.
  - Rezonality's seven render-test names are hardcoded in core (`do.py:1039-1045`).
  - Products also modify a core target: MegaCity's `Tests.cmake:36-38` adds a dependency and compile definitions to `draxul-test-app`.
  - CI selects tests with `--label-regex unit`, so CI and the agent gate run different inventories. `tests/do_py_tests.py` asserts the hardcoded list rather than parity with CMake.
- **Proposed boundary:**
  - Put a scope label in `draxul_add_test_target`: `core`, or the product name.
  - Add a `draxul_add_product_test_aggregate(scope targets…)` helper to `DraxulPlugins.cmake`, called from each product's `Tests.cmake`.
  - `do.py` then selects by `--label-regex ^core$` or `^<scope>$` only. Render scenarios already carry `test_scope` (root `CMakeLists.txt:505-508`); fill it in for the MegaCity and ScoreView scenarios.
- **Migration:**
  1. Add the labels.
  2. Switch `do.py` to labels and keep the old regex as an assertion for one release.
  3. Delete the per-product knowledge from `do.py` and `tests/CMakeLists.txt`.
  4. Change `do_py_tests` to compare against the `ctest -N -L` output.
- **Testing and agent benefit:** New test targets and goldens are picked up without edits to core, and the agent gate matches CI.
- **Risks:** Labels must be unique per scope. Platform-only render scenarios still need their `platforms` filter.

### 3. P1: `app/app.cpp` (5,931 lines) and `App` (about 60 fields) are the main collision point
- **Location:**
  - `app/app.h:101` (`friend struct AppTestAccess`) and `:355-390` (remote-topology, markdown-preview, agent and server-status state side by side).
  - `app/app.cpp` sections:
    - 3334-4140: remote topology reconcile.
    - 4248-~4628: `apply_local_topology_mutation`, a 16-case switch.
    - 4992-5263: agent launch and restart.
    - 5263-5375: session checkpointing.
    - 5562-5818: `handle_control_request`, with 4 ad-hoc locator lambdas.
  - `tests/active_tab_invariant_tests.cpp:19-160` pokes private fields.
- **Problem:** Remote-epoch handling resets Markdown-preview state (`app.cpp:3461-3467`), which is hidden cross-coupling. Tab and pane lookup is re-scanned 26 times through the mutable `TabController::tabs()` (`tab_controller.h:74-81`).
- **Proposed boundary:** app-local classes in their own translation units, still inside `draxul-app`:
  - `RemoteTopologyReconciler` (owns the snapshot, `TopologyProjection` and the announce flags).
  - `LocalTopologyApplier` (plugs into the existing `LocalTopologyMutationRoute::Handler`).
  - `ControlRequestEffects`.
  - `AgentLaunchCoordinator`.
  - `SessionCheckpointer`.
  - Prerequisites: `TabController::find_tab` and `SpaceController::locate_pane(pane_id)`, with `tabs()` made const.

  These depend on `SpaceController`, `AgentController` and client/protocol types. They must not depend on `IWindow` or the renderer.
- **Migration:**
  1. Add the lookup helpers.
  2. Move the reconciler verbatim; about 800 lines with self-contained state.
  3. Move the local applier.
  4. Replace `AppTestAccess` accessors as each class lands.
  5. Keep call order (`initialize` 499-537, `pump_once`) inside App.
- **Testing:** Snapshot-in/state-out tests with `FakeHost` and no App.
- **Agent benefit:** Remote-topology, agent, control and GUI work stop editing the same two files.
- **Risks:** No platform code is involved apart from `macos_menu_`. Initialisation ordering is the main hazard.

### 4. P1: Headless CLI code is compiled into the graphics app library and cannot be tested
- **Location:**
  - `app/CMakeLists.txt:9-37,39,41-64`.
  - `app/control_cli.cpp:3-6` and `app/topology_cli.cpp:3-10`: their includes are only config-document, control-plane, server/topology/remote-terminal client, plugin_manager and topology protocol.
  - `topology_cli.cpp:441-805` (parser) and `:805-1385` (a 580-line `run_topology_cli`); `control_cli.cpp:605-951`.
  - Duplicated helpers: `parse_int` and `parse_duration_ms` appear in both files (`control_cli.cpp:24,48` and `topology_cli.cpp:78,109`).
  - Divergent `DRAXUL_SERVER_RUNTIME_DIR` handling: `topology_cli.cpp:131-144` honours it; `control_cli.cpp:694-700` and `main.cpp:181-187` do not.
  - `app/cli_args.h:44-49`: the layout of `ParsedArgs` depends on `DRAXUL_ENABLE_RENDER_TESTS`. The define is PUBLIC on `draxul-app` only when the option is ON, while tests always define it (`tests/CMakeLists.txt:279`). Every preset turns it ON, so the mismatch is latent.
- **Proposed boundary:**
  - A `draxul-app-cli` static library in `app/cli/` holding `control_cli`, `topology_cli`, `cli_args`, `cli_help`, `session_id`, `agent_integration`, and a `cli_common` (int/duration parsing, env-injected runtime-dir and session resolution, and one noun/verb ownership table).
  - Its PRIVATE deps are client, control, protocol, plugin, host-api, agent-integration and json. Add it to the headless closure in `CheckDependencyBoundaries.cmake`.
  - Make the `ParsedArgs` fields unconditional.
- **Migration:** Move the files, then add a `draxul-test-cli` target, then make `run_*_cli` take a `CliIo` and a client facade.
- **Testing:** Parser and grammar tests stop linking the GPU stack. The 365-line topology parser gets its first unit tests.
- **Risks:** `session_id.cpp` reaches `pane_manager.h` through `app/session_state.h`, so point it at `<draxul/session_state.h>`. Keep Windows `ensure_console_io` in `main.cpp`.

### 5. P1: `app/main.cpp` (1,335 lines) holds untestable process-mode logic
- **Location:**
  - `app/main.cpp:308-605` (`run_server_mode`, including a 70-line status `printf`).
  - `:817-1073` (shared-server bootstrap, capability gating, new-session creation).
  - `:1078-1208` (the `ParsedArgs`→`AppOptions` mapping and the fake-remote factory).
  - `:772-784` (host registration, duplicated in `tests/test_main.cpp:50-56`).
  - `:1017-1023` (re-implements `make_unique_session_id` from `session_id.cpp:90`).
  - Root `CMakeLists.txt:254-257` (`main.cpp` is compiled only into the executable).
- **Proposed boundary:** `server_mode.{h,cpp}` (with a pure status formatter), `client_bootstrap.{h,cpp}` (with `ServerClient` behind an interface), `launch_options.{h,cpp}` (a pure `make_app_options`), and `register_app_host_providers(...)`. They go in Finding 4's target or a small `draxul-app-launch` target.
- **Migration:** Move one function at a time. Keep the `execv`/detached-helper `#ifdef`s in `main.cpp`.
- **Testing:** Table tests for option mapping and status formatting.
- **Agent benefit:** `main.cpp` stops being where every new flag lands.
- **Risks:** Windows and macOS helper hops must stay in `main`.

### 6. P1: `RemoteTerminalHost` contains a second full terminal transport
- **Location:**
  - `libs/draxul-host/src/remote_terminal_host.cpp:49-93` (classifiers and `RemoteHostCommand`) and `:131-1162` (worker, retry, requeue, coalescing).
  - These duplicate `libs/draxul-client/src/remote_session_coordinator.cpp:59-102,110-1101`: the same helper names and the same merge/resize/scroll-sum coalescing.
  - `app/main.cpp:1141-1168` still builds a coordinator-less host (`method_prefix = "fake"`).
  - `tests/remote_terminal_host_tests.cpp:256-1078` mostly exercises the legacy path.
- **Problem:** Remote-terminal protocol changes have to be made twice, and the path used in production is less tested than the legacy one.
- **Proposed boundary:**
  - `RemoteTerminalHost` becomes a thin adapter over `RemoteSessionCoordinator::Registration`.
  - Extract a pure `RemoteSnapshotApplier(Grid&, HighlightTable&)` from the pump (`:1199-1370`) and `compose_scrollback_view` (`:97-127`).
  - Move the duplicated paste-confirm gate (`:1478-1524` vs `terminal_host_base.cpp`) into `TerminalSurfaceHostBase`.
  - terminal-host → client stays PRIVATE.
- **Migration:**
  1. Either list the fake terminal in `session.terminals` or retire the fake mode. This needs an owner decision; see lead L1.
  2. Switch `main.cpp` to the coordinator.
  3. Port the legacy-path tests.
  4. Delete about 700 lines.
- **Testing:** The applier and paste gate can be unit-tested against a plain `Grid`.
- **Risks:** No platform code is involved.

### 7. P1: The wire contract is string-typed across app, client and server
- **Location:**
  - Capability lists are maintained by hand twice: `libs/draxul-client/src/server_client.cpp:568-595` and `libs/draxul-server/src/server_kernel_requests.cpp:50-83`.
  - App code searches for literals: `app/main.cpp:888-973,1154-1157` (18 `capabilities` checks), `app/app.cpp:3263,3359,3385` and `app/topology_cli.cpp:870`.
  - The only constant is `kServerClientTokenCapability` (`libs/draxul-protocol/include/draxul/server_protocol.h:29`).
  - Method literals such as `"agent.send_keys"` are repeated across `control_cli`, the router, the policy and the server.
  - Terminal request params are built by hand twice (the coordinator's `take_stream_command` at 454-541 and `remote_terminal_client.cpp:457-531`) and parsed by hand once (`remote_terminal_service.cpp:1109-1158`). `remote_terminal_protocol.h` has codecs for responses only.
- **Proposed boundary:** Add `server_capabilities.h` (constants plus a typed `ServerCapabilities` parsed once from the hello) and `control_methods.h` to `draxul-protocol`. Add request structs with `to_json`/`from_json` to `remote_terminal_protocol.h`. No new dependency edges.
- **Migration:** Add the constants, replace literals file by file, then add the request codecs.
- **Testing:** Protocol round-trip tests go in `draxul-test-protocol` (Finding 1), plus a test that the server's capabilities are a superset of what the client requires.
- **Risks:** None.

### 8. P1: The server request dispatcher is one ~1,070-line `if` chain over a fully public `Impl`
- **Location:**
  - `libs/draxul-server/src/server_kernel_requests.cpp:205-1277`: 37 `request.method ==` branches, with inline agent parameter parsing and an idempotency cache (`:551-623`).
  - `dispatch_stream_command` (`:169-203`) re-enters the chain.
  - `server_kernel_impl.h:61-278`: every field is public.
- **Proposed boundary:** A method→handler table plus per-domain handler translation units (agents, panes, sessions/UI). Agent request DTOs move to `draxul-protocol/agent_protocol.h`. `ServerSession` exposes narrow accessor structs.
- **Migration:** Pin the route table with a method-inventory test, then move one route family at a time.
- **Testing:** Agent request parsing can be tested without a kernel.
- **Agent benefit:** Server features stop sharing a single function.
- **Risks:** None platform-specific.

### 9. P1: The checkpoint pipeline is spread across three translation units and only testable through a live kernel
- **Location:**
  - `server_kernel_impl.h:240-268`: string state `"pending"`/`"writing"`/`"ok"`/`"failed"`.
  - `server_kernel_sessions.cpp:922-1064`: a detached writer thread.
  - `server_kernel_lifecycle.cpp:610-637` and `:651-705`.
  - `tests/server_session_checkpoint_tests.cpp`: 14 cases driven through `ServerKernel` and IPC.
- **Proposed boundary:** A private `SessionCheckpointer` in the server with an injected save function, clock and executor. The hooks already exist at `server_kernel.h:88-95`. The kernel calls `tick`, `request` and `flush(deadline)`.
- **Testing:** State-machine tests with a fake executor and clock. Keep only a few end-to-end cases.
- **Risks:** The Windows `MoveFileExW` semantics stay in the save function.

### 10. P1: The PTY layer has no platform-neutral interface, and a public header exposes `<windows.h>`
- **Location:**
  - `libs/draxul-terminal-process/include/draxul/conpty_process.h:17-27,66` (`WIN32_LEAN_AND_MEAN`, `<windows.h>`, `extern CreatePseudoConsole`, `cancel_pending_write(HANDLE)`).
  - The `spawn` signatures differ from `unix_pty_process.h:34-37`.
  - `server_terminal_runtime.h:9-13,129-133` (a `using Process = …` switch).
  - `server_terminal_runtime.cpp:281-402` (two `#ifdef` spawn bodies).
  - `server_kernel_terminals.cpp:127` (hard-coded `make_unique<ServerTerminalRuntime>`).
- **Proposed boundary:**
  - A public `ITerminalProcess`, `TerminalProcessSpawnOptions` and `create_terminal_process()`.
  - The ConPTY and Unix headers move to `src/`, behind the existing `-test-internals` target.
  - A pure `resolve_server_shell(kind, Platform)`.
  - A `terminal_runtime_factory` in `ServerKernelOptions`.
- **Testing:** Shell-resolution tests for both platforms can run on either OS. Kernel tests use the existing `FakeTerminalRuntime`/`StaticRemoteTerminalRuntime` instead of real shells.
- **Agent benefit:** Windows and macOS PTY work no longer collide in `server_terminal_runtime.cpp`.
- **Risks:** Write-cancel on Windows vs close on POSIX needs a `request_shutdown` hook behind the interface.

### 11. P1: Client control requests have no injectable seam, and the recovery ladder is duplicated
- **Location:**
  - `libs/draxul-client/src/server_control_channel.cpp:73-96` vs `remote_terminal_client.cpp:587-631`: the same token, handshake and refresh-retry logic.
  - `ControlClient::request` is static (`libs/draxul-control/include/draxul/control_plane.h:178-187`), so the client side has no seam comparable to the server's `ServerTransport`.
  - Error classifiers are copied between the coordinator (`:59-77`), `remote_terminal_host.cpp:49-72` and `client_recovery`.
- **Proposed boundary:** `ServerControlChannel` takes a `ControlRequester` (default: `ControlClient::request`), and `RemoteTerminalClient` and `ServerClient` compose it. Error classification lives only in `client_recovery.h`.
- **Testing:** A scripted requester replaces real sockets and pipes.
- **Risks:** Keep Windows `namespaced_control_id` inside the default requester.

### 12. P1: `draxul-session-model` mixes value types with the TOML codec and durable file I/O
- **Location:**
  - `libs/draxul-session-model/CMakeLists.txt:7-18`.
  - `libs/draxul-protocol/CMakeLists.txt:16-21` links it PUBLIC.
  - `session_state.cpp` (1,414 lines) covers validation (519-1087), path/slug policy and Windows/POSIX durable writes (1126-1310).
  - Protocol and client need only `session_model.h`.
- **Proposed boundary:** `draxul-session-model` becomes types plus pure validation. A new `draxul-session-store` holds the codec and file I/O and is linked only by the server and app. Update `CheckDependencyBoundaries.cmake` (the closure and headless lists).
- **Testing:** A codec and validation target that doesn't touch the protocol graph.
- **Risks:** The Windows replace path moves unchanged.

### 13. P1: Atomic temp-file-plus-replace is implemented about six times with different guarantees
- **Location:**

  | File | Approach |
  |---|---|
  | `session_state.cpp:1126-1310` (temp name set at `:1265`) | fixed `.tmp` name, fsync |
  | `agent_integration.cpp:179-196` | `.draxul.tmp`, no fsync |
  | `plugin_storage.cpp:51-255` | exclusive temps plus a `PluginStorageFileOperations` seam |
  | control transport `posix` and `win32` | 0600 permissions |
  | `config_document.cpp:122` | `MoveFileExW` replace |
  | `server_client.cpp:93-115` | helper copy |

  Done card `kanban/done/01 plugin-storage-exclusive-temporary-files -bug.md` shows this drift has already caused a bug.
- **Proposed boundary:** A small `draxul-atomic-file` static library with separate Windows and POSIX translation units. API: `write_file_atomically(path, bytes, {fsync_dir, permissions, exclusive_temp})`. Promote the plugin-storage seam as its fault-injection interface.
- **Migration:** Session store first, then agent-integration, then plugin storage, then control (keeping its staged error codes through a callback).
- **Testing:** One fault-injection suite replaces several ad-hoc ones.
- **Risks:** Windows ACL handling must stay parameterisable.

### 14. P1: Adding a GUI action means editing four shared files through a 52-field callback bag
- **Location:**
  - `app/gui_action_handler.h`: 52 `std::function` fields in `Deps`.
  - `libs/draxul-config/include/draxul/gui_actions.h:10-12,34`: `kGuiActions` has 57 rows, with a comment saying to "add a row… and a dispatch entry".
  - `app/app.cpp:950-1460`: the wiring. `on_split_vertical` and `on_split_horizontal` are near-copies (961-1000).
- **Proposed boundary:** `GuiActionHandler::register_command(name, fn)`, plus app-local command modules (`ServerCommands`, `SpaceCommands`, `AgentCommands`, `PaneLayoutCommands`) built on Finding 3's classes. A startup assert checks that every `kGuiActions` row is registered.
- **Migration:** One group at a time, starting with the server actions.
- **Testing:** Each module gets tests with fake ports. The existing handler and keybinding round-trip tests stay.
- **Risks:** Confirm how `macos_menu.mm` enumerates actions (lead L3).

### 15. P1: The "neutral" render contract carries Vulkan/Metal, and all plugin-support leaves share one include root
- **Location:**
  - `libs/draxul-plugin-support/CMakeLists.txt:19-27`: `draxul-plugin-render-support` INTERFACE-links `Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator` on Windows and `-framework Metal` on Apple.
  - Every leaf publishes the same `include` directory (`:6,20,…`).
  - The allowlist check in `cmake/DraxulPlugins.cmake:195-233` walks link libraries only, never include directories.
- **Problem:** Linking only `Adapter` still lets code include `vk_render_context.h`, `camera_input.h` and so on. Core targets such as ui and gui inherit native GPU link dependencies from a contract that is supposed to be backend-neutral.
- **Proposed boundary:**
  - `draxul-render-contract` (`base_renderer.h`, types only).
  - `draxul-render-context-vulkan` and `draxul-render-context-metal`.
  - One include root per leaf.
  - `Draxul::PluginSupport::Render` stays as an aggregate alias.
- **Migration:**
  1. Move headers per leaf, using forwarding headers for one step.
  2. Retarget ui, gui and nanovg.
  3. Add per-leaf link-isolation translation units.
- **Testing:** `draxul_reject_transitive_links(draxul-render-contract Vulkan::Vulkan)`.
- **Agent benefit:** A leaf's visible surface equals its link surface.
- **Risks:** The Apple contract must not pull in `Metal.h`. The existing `__OBJC__` guards make this mechanical.

### 16. P1: `markdown-host` and `plugin-host` link the full grid renderer but use only the contract
- **Location:**
  - `modules/markdown/draxul-markdown/CMakeLists.txt:45-49` (`PUBLIC … draxul-renderer`). No Markdown source references `renderer.h` or `IGridRenderer` (checked across `modules/markdown/`).
  - `libs/draxul-host/CMakeLists.txt:97-103` together with `src/plugin_host.cpp:10` (`#include <draxul/renderer.h>`), while the code uses only `base_renderer.h` and `IImGuiHost`.
- **Problem:** Markdown and plugin-host consumers and tests inherit the renderer's vk-bootstrap, ImGui backends and SDL. Markdown gets SDL only transitively through the renderer.
- **Proposed boundary:** Link the contract, the backend context leaf, `SDL3::Headers` and imgui-core instead.
- **Migration:** Swap the link lines and the include, then add both targets to the render-contract consumer loop in `CheckDependencyBoundaries.cmake:180`.
- **Testing:** `draxul-test-markdown-kanban` sheds the renderer.
- **Agent benefit:** Renderer edits stop rebuilding Markdown and the plugin host.
- **Risks:** Low. The backend pass files keep their native links.

### 17. P1: Plugin frame↔render-context mapping, pass recording and input translation are hand-copied in each product
- **Location:**
  - Host packing: `libs/draxul-host/src/plugin_render_pass_vk.cpp:30-56` and `plugin_render_pass_metal.mm:19-38`. These hardcode `1.0f, 96.0f` while `plugin_host.cpp:416-427` sends the real scale and PPI.
  - Product unpacking plus pass recording: `plugins/megacity/src/megacity_plugin.cpp:341-373,394-417` and `plugins/satview/src/satview_plugin.cpp:393-423,445-485`. They use the 22-parameter positional `VkRenderContext` constructor (`vk_render_context.h:22-32`).
  - Input translation exists five times (MegaCity, SatView, ScoreView, PCBView, Rezonality adapters) with divergent coverage:
    - MegaCity handles `COMPOSITION` and `FOCUS` (`megacity_plugin.cpp:270-290`).
    - SatView handles focus-lost only and returns 1 for unknown kinds (`satview_plugin.cpp:336-344`).
    - ScoreView handles neither (`scoreview_plugin.cpp:299-300`).
- **Proposed boundary:** header-only helpers in the allowlisted leaves:
  - `vk_plugin_frame.h` with `make_plugin_frame`, `context_from_plugin_frame` and `record_pass_in_plugin_frame`, plus a Metal twin.
  - A `VkRenderContext::Desc` aggregate to replace the positional constructor.
  - `plugin_runtime_input.h` with `dispatch_runtime_input(runtime, event, origin)`, installed with the SDK like `plugin_runtime.h`.
- **Migration:** Land the helpers in core and switch the host passes. Then MegaCity, SatView, ScoreView input, and PCBView (Finding 44), each as a product-repo commit.
- **Testing:** A GPU-free context→frame→context round trip per backend, and an input-mapping test covering every event kind.
- **Risks:** Preserve the Metal nil-encoder prepass and Vulkan no-active-pass prepass semantics exactly. Run the product goldens on both platforms.

### 18. P1: Plugin loader/host tests are a 2,113-line monolith inside the app test target, reaching into private sources
- **Location:**
  - `tests/CMakeLists.txt:43` and `:360-361` (`target_include_directories(draxul-test-app PRIVATE libs/draxul-host/src)`).
  - `tests/plugin_manager_tests.cpp:7` (`#include "plugin_storage.h"`), `:305-857` (storage), `:859+`, `:951+` and `:1181+` (MegaCity, ScoreView and SatView cases).
  - `libs/draxul-host/CMakeLists.txt` has no `-test-internals` target.
  - Also misfiled: `session_state_tests.cpp` in the app target.
- **Proposed boundary:**
  - Add a `draxul-plugin-host-test-internals` INTERFACE target.
  - Add a `draxul-test-plugin` target linking `draxul-plugin`, `draxul-plugin-host` and the fixture MODULEs (`tests/CMakeLists.txt:415-468`).
  - Split the file into storage, discovery and lifecycle suites.
  - Move each product's cases into that product's registered test sources.
- **Testing and agent benefit:** Plugin-ABI work no longer builds the app. Product and core agents stop sharing one file.
- **Risks:** Keep the platform spawn/dlopen helpers in shared test support.

### 19. P1: Source-text assertion tests pin implementation lines and block safe refactoring
- **Location:** `tests/nanovg_vulkan_sync_tests.cpp`, for example `:259-277`. It reads `nanovg_vk.cpp`, `nanovg_pass_vk.cpp` and `vk_renderer.cpp` as text and requires literals such as `"toTransfer.oldLayout = tex.layout"`. It has 11 source-path reads and 28 `.find` checks, and mixes NanoVG, VkRenderer present/capture (through the good `VkRendererOperations` seam) and CMake pins.
- **Proposed boundary:** Split the file into a seam-based frame-transition suite, NanoVG behaviour suites (through a `NanoVGVkOperations` seam or Finding 20's recorder) and a CMake-pin check.
- **Migration:** Convert this file before Findings 20 and 22. Delete each text check once a behaviour test covers it.
- **Agent benefit:** Refactors stop failing on renames or whitespace.
- **Risks:** Add a Metal-side seam at the same time for parity.

### 20. P1: The NanoVG Vulkan and Metal backends duplicate the backend-neutral call recorder
- **Location:** `libs/draxul-nanovg/backend/src/nanovg_vk.cpp:1142-1294` (`fanToTriangles`, `renderFill`/`Stroke`/`Triangles`) and `nanovg_mtl.mm:636-780`. The code is the same apart from the prefix.
- **Precedent:** `nanovg_paint.h` was already extracted and is tested by `draxul-test-nanovg-paint` (`tests/CMakeLists.txt:390-393`).
- **Proposed boundary:** `nanovg_calls.{h,cpp}` inside `draxul-nanovg-backend` (calls, paths, vertices and the uniform arena). Backends keep only blend translation, textures and flush encoding. No new dependencies.
- **Testing:** New cases in `draxul-test-nanovg-paint` that run on both OSes.
- **Risks:** Metal's `maxVertCount` pre-sizing stays backend-owned. Depends on Finding 19.

### 21. P1: The two Markdown render passes duplicate planning and have already diverged between platforms
- **Location:**
  - `modules/markdown/draxul-markdown/src/markdown_render_pass_vk.cpp`: clamps invalid upload rects at `:691-694`, uses `VK_FILTER_LINEAR` at `:270-271`, and `std::max`s the revision at `:798`.
  - `markdown_render_pass_metal.mm`: rejects via `valid_upload_rect` at `:28,251`, uses `Nearest` at `:109-110`, and assigns the revision at `:356`.
  - `instance_range_valid` is duplicated (vk `:64`, mtl `:38`), and so are the `PushConstants` structs, with no shader contract.
  - Markdown shaders are compiled and staged by root CMake (`cmake/CompileShaders_Metal.cmake`, root `CMakeLists.txt:341-349`).
- **Proposed boundary:**
  - A CPU-only `markdown_gpu_plan.{h,cpp}` in `draxul-markdown` (atlas create/copy/revision decisions and validated batches). Backends only encode.
  - A `shaders/contracts/markdown.toml` parity test.
  - Shaders move into the module (Finding 36).
- **Testing:** Planner tests in `draxul-test-markdown-layout`, which links only `draxul-markdown`.
- **Risks:** Clamp-vs-reject and the sampler filter must be chosen deliberately, since they differ today (lead L4).

### 22. P1: The renderer backends are large mixed translation units, platform-asymmetric, and use friend access
- **Location:**
  - `libs/draxul-renderer/src/vulkan/vk_renderer.cpp`: 153-380 (`VkGridHandle` reaching into `ctx_`, `desc_pool_`, `pipeline_` and `atlas_` through friendship; `vk_renderer.h:64-66`), 495-583 (capture), 900-1020 (ImGui).
  - `metal_renderer.mm`: 253-439 (`initialize`) and 674-801.
  - The BGRA→RGBA swizzle is duplicated (VK 571-578, Metal 750-763).
  - Metal has no operations seam.
- **Proposed boundary:** Per-concern files inside the existing targets: `vk_grid_handle`, `vk_capture` and `vk_imgui_backend`, with Metal mirrors. `VkGridHandle` takes a narrow `VkGridResources` view. Shared CPU helpers go in `src/shared/capture_convert.h`, plus a `MetalRendererOperations` seam.
- **Migration:** Move verbatim after Finding 19, then narrow the interfaces.
- **Testing:** Swizzle/pitch tests, and Metal transition tests that mirror the Vulkan ones.
- **Risks:** Metal capture's `waitUntilCompleted`-before-signal ordering (735-771) must be preserved.

### 23. P1: Agent guidance is stale or missing where products and tooling are most unusual
- **Location:**
  - `CLAUDE.md:259-262` lists `--megacity|--satview|--scoreview|--pcbview` but omits `--rezonality`, which `do.py:950` supports.
  - `CLAUDE.md:118` and `AGENTS.md:51-52` point only at MegaCity's guide, not at `plugins/satview/AGENTS.md` or `plugins/rezonality/AGENTS.md`.
  - `CLAUDE.md:265-270` recommends the `draxul-test-core` narrow loop, which Finding 1 shows is not narrow.
  - `GEMINI.md:11-12` points at `docs/features/`, which is absent from the tree.
  - `plugins/megacity/product/AGENTS.md:11-27,87-92` omits `draxul-megacity-model` and its test targets. It also lives under `product/` although it governs `src/`, `tests/`, `shaders/` and `cmake/`, so hierarchical guide discovery misses it for those files.
  - `plugins/megacity/README.md:148-150`'s `ctest -R draxul-test-megacity` also matches suites the preceding command didn't build.
  - ScoreView has no AGENTS.md, despite its standalone/extraction mode, SDL headers-only linkage on macOS and Verovio DLL copy on Windows. PCBView has none either.
- **Proposed fix:**
  - Move the MegaCity guide to `plugins/megacity/AGENTS.md` and correct it.
  - Add short ScoreView and PCBView guides covering boundaries, targets, the exact test command and platform pitfalls.
  - Add a single "product guides" line in `CLAUDE.md`, add `--rezonality`, and fix the narrow-loop example after Finding 1.
- **Benefit:** Removes wrong commands, the most direct cause of false-green handoffs.
- **Risks:** None.

### 24. P1: The MegaCity renderer duplicates its uniform ABI and per-frame preparation across Vulkan and Metal
- **Location:**
  - `plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_render_vk.cpp:27-70` (glm structs with defaults), `173-213` (helpers), `235-3288` (a ~3,050-line `State`), `3305-3966` (`record_prepass`).
  - `codeviz_render.mm:28-71` (`simd_*` twins without defaults) and `1145-1815`.
  - SatView ice-box card 42 covers SatView only.
- **Proposed boundary:** A `codeviz_frame_contract.h` (glm-typed, with `static_assert`s on sizes and offsets) and a pure `codeviz_frame_prep.cpp` (`build_frame_uniforms`, material uniforms, AO radius). Both go in the renderer's shared source list and are tested through the existing `draxul-codeviz-renderer-test-internals`.
- **Risks:** Vulkan Y-flip and shadow bias stay per backend. Check the MegaCity goldens on both platforms.

### 25. P1: The SatView runtime mirrors every config field and assembles the frame inside `draw()`
- **Location:**
  - `plugins/satview/src/runtime/satview_runtime.cpp:1807-1859` (`current_config`, about 48 member→field copies), `1861-1966` (`apply_config`), `895-1383` (`draw`, about 30 `scene_pass_->set_*` calls at 1081-1175).
  - `satview_scene_pass.h:82-378` (CPU state inside the pass).
  - Done bug cards 15 and 16 were caused by exactly this mirrored state.
- **Proposed boundary:** The runtime holds a single `SatViewConfig settings_`, and `apply_config` diffs it to trigger side effects. A pure `SatViewFrameBuilder` in `draxul-satview-scene` produces a `SatViewFrameDescription`, and `SatViewScenePass::submit(desc)` replaces the setters.
- **Testing:** Frame-description unit tests without ImGui or a GPU.
- **Risks:** Keep per-stream revisions so each backend uploads only what changed. This is separate from cards 40 and 42.

### 26. P2: Plugin ImGui ships a private fork of `imgui_impl_vulkan` that products call through the upstream header
- **Location:**
  - `plugins/support/imgui/src/imgui_impl_vulkan.h:110-122` (added `RetireFontsTexture`, `SetExternalCommandBuffer` and `AddTexture`) and `CMakeLists.txt:25-30`.
  - The host compiles upstream (`libs/draxul-renderer/CMakeLists.txt:62-69`).
  - Products include `<backends/imgui_impl_vulkan.h>` (`codeviz_render_vk.cpp:11`, `satview_render_vk.cpp:8`) but link the fork.
- **Proposed boundary:** Move the fork into `libs/draxul-imgui-core/backend/` with renamed symbols, behind a `register_texture`/`unregister_texture` wrapper. Add a check that rejects product includes of the upstream backend header.
- **Risks:** The Metal side is intentionally per-plugin (ObjC class renaming). Duplicate-symbol behaviour on Windows is unverified (lead L5).

### 27. P2: `PaneManager` and `render-test` depend on the whole `AppOptions`, which lives in a grab-bag library
- **Location:**
  - `app/pane_manager.h:39-73`.
  - `pane_manager.cpp:1354-1365`: three-way host-factory precedence, synced back by `app.cpp:319-321`. Launch defaults are assembled four times.
  - `libs/draxul-runtime-support/include/draxul/app_options.h:26-112`: window and renderer factories and `ServerWelcome`, next to an unrelated cursor blinker, UI worker and resource monitor.
  - `libs/draxul-render-test/include/draxul/render_test.h:5-6`.
- **Proposed boundary:** A `PaneLaunchDefaults` value, a single resolved `HostFactory`, and an injected `const HostProviderRegistry&`. `AppOptions` becomes app-owned, and `render_test` returns a neutral `RenderScenarioOptions`.
- **Testing:** `pane_manager_tests` stops needing the global registry.
- **Risks:** Keep the precedence of the test override.

### 28. P2: Server helper paths and launch argv are split by platform across layers
- **Location:**
  - `app/macos_server_status_surface.mm:88-110` (pure `std::filesystem` path logic next to an NSApplication delegate) vs its Windows twin in `libs/draxul-client/src/server_client.cpp:506-531`.
  - `server_client.cpp:1013-1101` builds the launch argv twice (a `CreateProcessW` string and a POSIX vector), although `spawn_detached` already supports Windows (`draxul-types/process_util.h:88-112`).
  - `app/server_status_surface.h:20-39` mixes pure formatters with SDL tray code.
- **Proposed boundary:**
  - A `server_executable_layout.{h,cpp}` in `draxul-client`, compiled on every platform so both paths are tested everywhere.
  - A single `server_launch_arguments(options)`.
  - The formatters move to headless code (Finding 4 or 5).
- **Risks:** Replace `_wcsicmp` with a portable compare, and keep the bundle layout in sync with root `CMakeLists.txt:311-325`.

### 29. P2: Config load and reload are duplicated, and the reload diff is buried in App
- **Location:** `app/app.cpp:351-387` and `:680-689` (the same load sequence); `:78-87`, `:251-275`, `:692-695` and `:744-746` (change detection).
- **Proposed boundary:** `load_effective_config(path, overrides)` and a pure `ConfigReloadDelta diff(old, new)`.
- **Testing:** Replaces full-App reload tests (`config_lifecycle_tests`) with pure delta tests.

### 30. P2: `draxul-app` exposes its whole source directory and every link publicly
- **Location:** `app/CMakeLists.txt:39` (PUBLIC include of `app/`) and `:41-64` (22 PUBLIC links). `app.h` pulls in 15 app headers plus the renderer, window and text service.
- **Proposed boundary:** After Finding 3, give `App` a pimpl, make the include directory PRIVATE, add `draxul-app-test-internals`, and demote links to PRIVATE.
- **Benefit:** Editing an internal header stops rebuilding every app test.

### 31. P2: `RemoteSessionCoordinator` mixes three responsibilities and is coupled both ways with `RemoteSessionClient`
- **Location:**
  - `remote_session_coordinator.cpp`: 184-330 (coalescing under a mutex), 1548-2410 (stream I/O), 2430-2560 (poll worker).
  - `remote_session_coordinator.h:36` (a raw `RemoteSessionClient*`).
  - `remote_session_client.h:30` (`externally_fed` dual mode).
  - The tests build real `ControlServer`s: 6 cases in 1,580 lines. `session_transport_load_tests.cpp:284-548` has a private harness.
- **Proposed boundary:** A pure `detail::TerminalCommandQueue` (following the precedent of `session_stream_policy.h`), a `SessionStreamConnection`, a `SessionProjectionSink` interface, and `tests/support/fake_session_server.h`.
- **Depends on:** Finding 6, which removes the host-side copy first.

### 32. P2: Include-boundary enforcement has gaps, and tests reach private code by relative path
- **Location:**
  - All five host leaves share one PUBLIC include directory (`libs/draxul-host/CMakeLists.txt:91` and the other leaves).
  - No link-isolation translation units exist for control, session-model, agent, grid, or the grid, terminal, nvim and plugin host leaves (`tests/CMakeLists.txt:597-671`).
  - Relative private includes:
    - `tests/support/server_kernel_test_support.h:3-8`, `remote_session_client_tests.cpp:4` and `session_stream_policy_tests.cpp:1` (there is no `draxul-client-test-internals`).
    - `tests/nanovg_paint_tests.cpp:1,3` and `plugin_nanovg_pass_tests.cpp:1`.
    - `tests/support/kanban_directory_scan_test_support.h:3`.
- **Proposed boundary:** Per-leaf include roots or isolation translation units; `-test-internals` targets for client, nanovg-backend, plugin-nanovg and kanban; and a configure check that rejects `../libs/*/src` includes in `tests/`.

### 33. P2: Plugin pane id/config validators exist three times with different limits
- **Location:** `topology_service.cpp:77-100` (4,096-byte bound), `session_state.cpp:46-68` (no bound, and the reason session-model links json), and `plugin_manager.cpp:270-277`.
- **Proposed boundary:** A single pure validator plus a limit constant in session-model or protocol, tested in Finding 1's protocol target.

### 34. P2: The server has four request-idempotency caches with different key rules
- **Location:** `remote_terminal_service.cpp:210-257`, `server_kernel_requests.cpp:551-623`, `topology_service.cpp:1667-1676` and `session_stream_service.cpp:252-371`.
- **Proposed boundary:** A private `BoundedResultCache` plus a `parse_request_id` helper in the server, with focused tests.

### 35. P2: `NvimHost` has no transport injection
- **Location:** `libs/draxul-host/src/nvim_host.h:66-67` (concrete `NvimProcess` and `NvimRpc`) and `nvim_transport.h:59`. `tests/support/fake_rpc_channel.h` exists but cannot reach the host.
- **Proposed boundary:** Inject an `IRpcChannel` factory into `NvimHost`.
- **Testing:** Clipboard and action behaviour tested without an app.

### 36. P2: SDL is linked in full where only keycodes are needed
- **Location:** `libs/draxul-nvim/CMakeLists.txt:12` (nvim-protocol links `SDL3::SDL3`; `input.cpp` includes `<SDL3/SDL.h>`) and `modules/kanban/draxul-kanban/CMakeLists.txt:16-18` (keycodes only, in `kanban_navigation.cpp:5`).
- **Proposed boundary:** Use `SDL3::Headers` plus `SDL_keycode.h`, following `draxul-imgui-core`. Add SDL to the reject list in `CheckDependencyBoundaries.cmake:131-133` for the protocol leaf.

### 37. P2: There is no shared shader-build helper
- **Location:**
  - `cmake/CompileShaders_Metal.cmake:1-70` (three copied blocks with hand-listed dependencies).
  - `cmake/CompileShaders.cmake:11-33`.
  - `plugins/spinning-triangle/CMakeLists.txt:67-109`.
  - Product loops: `plugins/megacity/CMakeLists.txt:50-133`, `plugins/satview/CMakeLists.txt:146-213` (whose `DEPENDS` hardcodes one `.glsl`), `plugins/scoreview/CMakeLists.txt:170-247`, `plugins/pcbview/CMakeLists.txt:41-93` (identical NanoVG loops).
- **Proposed boundary:** `cmake/DraxulShaders.cmake` with `draxul_add_shader_library(<t> GLSL … METAL … INCLUDES … STAGE_DIR …)` and `draxul_plugin_nanovg_shader_payload`. Add a configure-test case like `tests/cmake/dependency_boundary`.
- **Risks:** ScoreView's standalone build needs the helper shipped with the SDK config, or it must stay optional there.

### 38. P2: `draxul-render-test` mixes a pure harness, a capture driver and a demo host
- **Location:**
  - `libs/draxul-render-test/CMakeLists.txt:1-31`: PUBLIC config, font, host-api, nanovg, renderer and runtime-support.
  - `render_test.h:6` includes `renderer.h` although `CapturedFrame` is in draxul-types.
  - `nanovg_demo_host.h:27` is an `IHost` registered by `main.cpp` and `test_main.cpp`.
- **Proposed boundary:** `draxul-render-harness-core` (scenario TOML, diff, BMP, JSON) and `draxul-render-test-driver`. `NanoVGDemoHost` moves to a host-side library.
- **Testing:** Parser and diff tests in a tiny target.

### 39. P2: Product render scenarios and references live in the core manifest
- **Location:** root `CMakeLists.txt:438-516` and `tests/render/manifest.json`, which includes `satview-*`, `scoreview-*`, `megacity-*`, `pcbview-*` and `rezonality-*` scenarios and their BMPs.
- **Proposed boundary:** A `RENDER_MANIFEST` argument to `draxul_register_bundled_plugin` (`DraxulPlugins.cmake:60-162`), so each product owns its scenarios and goldens.
- **Benefit:** Removes a shared JSON hotspot that every product agent edits.
- **Risks:** Keep the `<name>.<platform>.bmp` naming.

### 40. P2: `KanbanHost` mixes pure board/session state with painting, key repeat, file watching and preview
- **Location:** `modules/kanban/draxul-kanban/include/draxul/kanban/kanban_host.h:77-102` and `kanban_host.cpp:836-885,929-990,1117-1157`.
- **Proposed boundary:** A `KanbanSession` in `draxul-kanban` core whose commands return a `KanbanChange`. The host maps changes to redraw, preview and frame requests.
- **Testing:** Many `[kanban][host][input]` cases (`tests/kanban_host_tests.cpp:337-608`) move to the core target, which needs no renderer or font.

### 41. P2: The in-process and plugin NanoVG pass adapters duplicate frame driving
- **Location:**
  - `libs/draxul-nanovg/src/nanovg_pass_vk.cpp:26-88` plus its Metal twin.
  - The plugin path already factors this out (`plugins/support/nanovg/src/plugin_nanovg_pass_internal.h:11-70`) and has tests.
  - `plugin_nanovg_pass_vk.cpp:52-79` creates VMA by hand instead of calling `ensure_allocator`.
- **Proposed boundary:** A neutral driver in `draxul-nanovg-backend`, with both adapters as thin shims.

### 42. P2: The SDK install component is assembled implicitly from four `CMakeLists.txt` files
- **Location:** `sdk/CMakeLists.txt:25-35`, `libs/draxul-types/CMakeLists.txt:14-20`, `libs/draxul-performance/CMakeLists.txt:11-16` and `libs/draxul-plugin-support/CMakeLists.txt:14-17,42-45`. `plugin_runtime.h:15-16` forward-declares core C++ types.
- **Proposed boundary:** Declare the SDK file set in `sdk/`, as an optional `-cxx` component with exported targets, and add an install-tree compile test per shipped header.

### 43. P2: The MegaCity host embeds two hand-rolled worker lifecycles in its public header
- **Location:**
  - `plugins/megacity/product/draxul-megacity/include/draxul/megacity_host.h:181-225` (threads, mutexes, `RetiredGridThread`).
  - `megacity_host.cpp:592-760` and `789-866,1420-1483`.
  - The test uses `#define private public` (`plugins/megacity/tests/support/megacity_scene_test_support.h:35-37`).
- **Proposed boundary:** `city_grid_build_worker` and `city_route_worker` (request, poll, cancel, join, generation) in `src/`, plus a `MegaCityHostTestAccess` friend, matching the SatView and ScoreView seams.
- **Testing:** Stale-generation and shutdown tests without the host.

### 44. P2: Product tests depend on core host, renderer and config fakes, and the dependency check doesn't cover test targets
- **Location:**
  - `plugins/megacity/cmake/Tests.cmake:26-33` (links `draxul-config`, `draxul-host-api` and `draxul-renderer`).
  - `megacity_scene_test_support.h:14-15,28` (core `FakeWindow`/`FakeTermRenderer`, `app_config.h`).
  - `plugins/satview/tests/satview_host_fixture.h:24-28`.
  - `cmake/DraxulPlugins.cmake:235-279` checks `PRODUCT_TARGETS` only.
- **Proposed boundary:** A `tests/support/fake_plugin_frame.h` (`IFrameContext` over `base_renderer.h`), `PluginRuntimeTestCallbacks` used everywhere, and the dependency check extended to plugin test targets with a small allowlist.
- **Benefit:** Core host-API refactors stop breaking product test builds.

### 45. P2: Rezonality compiles its plugin twice, and the contract test mixes three kinds of test
- **Location:**
  - `plugins/rezonality/cmake/Tests.cmake:9-26`: recompiles `src/rezonality_plugin.cpp` and the native backend, is `RUN_SERIAL`, and depends on the module.
  - `tests/rezonality_plugin_contract_tests.cpp` (1,348 lines) mixes in-process, dlopen and pure cases.
  - All three Rezonality libraries publish `src/` PUBLIC.
- **Proposed boundary:**
  - An OBJECT library `draxul-rezonality-module-objects` shared by the MODULE and the test. It must be OBJECT, not STATIC, or the exported entry point is dropped.
  - Move the pure cases to the project, runtime and audio targets.
  - Split `include/` from `src/`.
- **Risks:** The Metal and Vulkan backend sources must stay paired.

### 46. P2: ScoreView header ownership and one pass-through dependency
- **Location:**
  - `plugins/scoreview/product/draxul-scoreview/CMakeLists.txt:46-52`: score-audio is PUBLIC "for the synth/listener headers the host's audio rig uses", but only runtime files include them.
  - The pipeline and runtime libraries share one PUBLIC `include/`.
  - `score_runtime.cpp:547-984` (layout and engrave orchestration) and `1416-1547` (gate mode).
- **Proposed boundary:** An `include-runtime/` directory, a direct runtime→audio link, and `ScoreLayoutController` and `GateSession` extractions.
- **Risks:** The standalone extraction smoke must still configure.

### 47. P2: The PCBView runtime takes raw ABI structs, and its autorouter mixes in presentation queries
- **Location:**
  - `plugins/pcbview/src/pcbview_runtime.h:38-52` (`DraxulPluginViewportV2`, `DraxulPluginInputEventV2`, and the frame structs).
  - `cmake/Tests.cmake:5-9` (tests link only the core library).
  - `src/autorouter.cpp:1472-1581` (visibility and picking).
- **Proposed boundary:**
  - Adopt the `PluginRuntime*` vocabulary and `dispatch_runtime_input` from Finding 17.
  - Move picking into `selection.{h,cpp}`.
  - Add `draxul-test-pcbview-runtime`.
- **Risks:** None platform-specific.

### 48. P2: Products use three different pane-persistence strategies, and SatView's adapter still has template leftovers
- **Location:**
  - `plugins/megacity/src/megacity_plugin.cpp:141-165`, `plugins/satview/src/satview_plugin.cpp:109-148` and `plugins/scoreview/src/scoreview_plugin.cpp:111-127`. ScoreView hand-rolls its own because `HostServices` isn't installed (`libs/draxul-plugin-support/CMakeLists.txt:4-9`).
  - `satview_plugin.cpp:81-83`: spinning-triangle `speed`, `angle` and `direction` are parsed, persisted and advanced but never rendered.
- **Proposed fix:** Make `HostServices`/adapter state installable or header-only, document one preference pattern, and delete the dead fields.

### 49. P2: Product test executables are monolithic
- **Location:**
  - `plugins/megacity/cmake/Tests.cmake:24-33`: geometry, lcov and config tests share the host-linked executable.
  - `plugins/satview/cmake/Tests.cmake:16-30`: pure propagation, catalog and geodetic tests link the runtime, renderer and SDL.
- **Proposed boundary:** `draxul-test-megacity-geometry` and `draxul-test-satview-core`, both under the product label.
- **Depends on:** Finding 2, so the new targets actually run.

---

## Proposed module and target map (target state)

```text
foundations: draxul-types · draxul-performance · draxul-bmp · draxul-host-identity
             + draxul-atomic-file (new)                                    [13]
wire:        draxul-session-model (types+validation) → draxul-session-store (new, codec/IO) [12]
             draxul-protocol (+server_capabilities.h, control_methods.h, request codecs) [7]
transport:   draxul-control (+ControlRequester seam) → draxul-client (single terminal transport) [6,11]
             draxul-terminal-process (ITerminalProcess; ConPTY/Unix private) [10]
server:      draxul-server (route table, SessionCheckpointer, BoundedResultCache) [8,9,34]
render:      draxul-render-contract → draxul-render-context-{vulkan,metal} [15,17]
             draxul-renderer (per-concern backend TUs) · draxul-nanovg-backend (+nanovg_calls) [20,22]
             draxul-imgui-vulkan-backend (renamed fork) [26]
             draxul-render-harness-core → draxul-render-test-driver [38]
hosts:       host-api · grid-host · terminal-host (thin remote adapter) · nvim-host (IRpcChannel) · plugin-host(+test-internals) [6,18,35]
modules:     draxul-markdown (+markdown_gpu_plan) → markdown-host (contract only) [16,21]
             draxul-kanban (+KanbanSession) → kanban-host [40]
app:         draxul-app-cli (new, headless) · draxul-app (reconciler/applier/effects/agent/checkpointer TUs, private includes) · draxul exe [3,4,5,30]
tests:       per-module draxul-test-* targets selected by CTest scope label; one integration target owns the dependency on `draxul` [1,2]
```

## Best isolated work packages for parallel agents

1. **Test-graph split and label-based scope** (Findings 1 and 2). This is CMake and `do.py` only, is guarded by the existing tripwires, and unblocks everything else.
2. **Wire constants and request codecs** (Findings 7, 33 and 34). Protocol-first and mechanical.
3. **`draxul-app-cli` and `main.cpp` extraction** (Findings 4 and 5). Touches `app/` only; `app.cpp` is left alone.
4. **`RemoteTerminalHost` → coordinator adapter** (Finding 6). Host and client only; needs the owner decision in lead L1.
5. **PTY interface and shell resolution** (Finding 10). terminal-process and the server runtime.
6. **Session-model/store split and atomic-file library** (Findings 12 and 13).
7. **Render-contract leaves, then retargeting markdown-host and plugin-host** (Findings 15 and 16).
8. **Converting the source-text tests, then extracting the NanoVG recorder** (Findings 19 and 20).
9. **Markdown GPU planner** (Finding 21). Module-local.
10. **Guide fixes** (Finding 23). Docs only; can start immediately.
11. **Product-repo packages:**
    - MegaCity: Findings 24 and 43.
    - SatView: Finding 25.
    - ScoreView: Finding 46.
    - PCBView: Finding 47.
    - Rezonality: Finding 45.

    Each is independent after the core helpers from Finding 17 land.

## Structural qualities worth preserving

- The CMake boundary machinery:
  - `CheckDependencyBoundaries.cmake` (headless closures and render-contract rejection).
  - The `DraxulPlugins.cmake` STRICT product-link allowlist.
  - The internal-target policy audit.
  - The positive and negative CMake tests.
- Test partition, duplication and static-inventory tripwires (`tests/CMakeLists.txt:111-259`), plus the link-isolation executables. These are what make the proposed splits safe.
- The versioned C-ABI SDK: `struct_size`, borrowed-handle rules, the C link-isolation test, and the external SDK smoke.
- Existing seams:
  - `VkRendererOperations`, `ITopologyMutationRoute`, the `ControlRequestPolicy`/`Router` split, `IRemoteTerminalRuntime` and `ServerTransport`.
  - `PluginStorageFileOperations` and the pure `SessionStreamPolicy`.
  - The `*-test-internals` INTERFACE pattern.
  - The shader-contract TOML parity tests.
- Completed consolidations:
  - The nvim protocol/transport split and the five host leaves.
  - `draxul-app-shell` as a pure library.
  - `TerminalSurfaceHostBase`.
  - The shared topology→layout conversion.
  - Product layering in MegaCity (model/geometry/scene/renderer), SatView (core/scene/services/runtime), ScoreView (link-enforced learn leaf) and Rezonality (pimpl backend, focused targets).

## Coverage

| Area | Representative paths and flows examined | Inspected by | Remaining gaps |
|---|---|---|---|
| Guidance, build and tooling | `CLAUDE.md`, `AGENTS.md`, `GEMINI.md`, module-map; root, `tests/` and `cmake/*` CMake; `do.py` test scope; `do_py_tests`; CI; `run_tests.*`; Kanban lanes; consolidation plans | Coordinator (plus D for products) | `review.py` and the other `scripts/*` only skimmed |
| App orchestration and config | `app/CMakeLists.txt`, `main.cpp`, `app.h`/`app.cpp` (init, reload, remote reconcile, control), CLIs, pane/tab/space controllers, GUI actions, status surface; app tests and fixtures | A; coordinator re-verified | Chrome, command palette, `input_dispatcher` internals, `config_schema.cpp`, window internals |
| Client, server, control and session | Coordinator, remote-terminal client/host, control channel, kernel requests/sessions/lifecycle, checkpoint, services, session model, control transport; test includes | B; coordinator re-verified | `server_agent_service`, `session_poll_service`, protocol codec internals, win32/posix `async_frame_stream` parity |
| Terminal, PTY, Neovim and input | PTY headers, server terminal runtime, nvim CMake/transport/host, VT test inventory | B | terminal-core/VT parser internals, ConPTY/Unix bodies, `mouse_reporter`, selection |
| Rendering, fonts and platform | Renderer CMake; `vk_renderer`, `metal_renderer`; plugin-support CMake/headers; NanoVG backends; ImGui support; render-test | C; coordinator re-verified | Font internals, VK helper internals, NanoVG flush bodies |
| Built-in modules | Markdown CMake/passes/host; Kanban host/store/CMake; file-monitor, http, weather (skimmed) | C | Markdown parser and layout internals |
| SDK and plugin loading | `sdk/`, `plugin_api.h`, plugin-host/render passes, `plugin_manager` tests, spinning-triangle (outline) | C, A | plugin-support leaf `.cpp` bodies |
| Products (MegaCity, SatView, ScoreView, PCBView, Rezonality) | CMake, `Tests.cmake`, AGENTS/READMEs, adapters, runtime/host outlines, VK/Metal renderer outlines, fixtures, Kanban cards | D; coordinator re-verified key items | SatView renderer bodies, ScoreView learn/audio, Rezonality `live_project` and backend bodies, MegaCity builders |

No subagent failed. All four returned inspected and uninspected lists, and their findings were accepted only after the coordinator re-read the evidence.

## Unresolved leads (not accepted as findings)

- **L1:** Is the `fake-remote-terminal` mode (`app/main.cpp:1141`) required outside diagnostics and demos? This decides whether Finding 6 retires it or teaches `session.poll` about it. Needs owner input and a `docs/features.md` check.
- **L2:** `draxul-protocol` links all of terminal-core but may only need `terminal_snapshot.h`. Needs a dependency read of `terminal_snapshot.cpp` before proposing a snapshot leaf.
- **L3:** How does `macos_menu.mm` enumerate actions: through `kGuiActions` or through handler `Deps`? This affects how safe Finding 14 is.
- **L4:** Are the Markdown sampler and upload-rect policy differences (Finding 21) intentional? Needs history or owner input.
- **L5:** Does linking both `draxul-renderer` (upstream ImGui Vulkan backend) and `draxul-plugin-imgui` (fork) into one executable, such as `draxul-test-megacity`, produce duplicate or ODR-violating symbols on MSVC? Needs a Windows link map.
- **L6:** Do the `megacity-plugin` and `scoreview-plugin` render scenarios, which lack `requires_target`, register CTests when their product is disabled? Needs a manifest-by-manifest read against root `CMakeLists.txt:463-515`.
- **L7:** PCBView is disabled in CI on both platforms (`build.yml:60,108`) and isn't checked out there, apparently because its repo is private. Whether it needs its own CI entry point is an owner decision.
