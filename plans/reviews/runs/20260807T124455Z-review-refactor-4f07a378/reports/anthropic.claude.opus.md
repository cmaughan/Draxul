# Draxul Structural / Refactoring Review

Scope: read-only inspection of `app/`, `libs/`, `modules/`, `tests/`, `scripts/`, `shaders/`, root build files, `AGENTS.md`, `modules/megacity/AGENTS.md`, and the full `kanban/` tracker. Findings below were checked against `kanban/pending/`, `kanban/ice-box/`, and `kanban/done/`; items already covered by the standing refactor backlog (cards 00–08 and ice-box 22/23/125) are excluded and summarized separately in the "Already tracked" appendix so sequencing stays coherent.

---

## F1 — `ServerKernel::Impl` is a six-responsibility god class with a 975-line request dispatcher (P1)

**Location** — `libs/draxul-server/src/server_kernel.cpp:339` (class `ServerKernel::Impl`), `:1899–2874` (`handle_request`), `:3216–3528` (`run_until_stopped`), `:1442–1808` (service/session/checkpoint), `:1570–1664` (client registry). Target `draxul-server`. Test: `tests/server_kernel_tests.cpp` (4928 lines, 61 `TEST_CASE`s).

**Current structural problem** — One private `.cpp`-local class owns, in a single mutex-guarded state bag: (1) process-singleton marker lifecycle (`publish_starting_marker`, `publish_failure_marker`, `epoch_value`, `pid`, `process_start_identity`); (2) the client registry with connection tokens, activity timeouts and pruning (`clients`, `client_sessions`, `register_client_hello`, `authenticate_or_touch_client`, `prune_inactive_clients`); (3) session lifecycle and checkpoint scheduling (`sessions`, `ServerSession::CheckpointTask`, `checkpoint_session`, `collect_checkpoint_results`); (4) terminal endpoint creation/destruction/restart plus managed-agent launch; (5) RPC method dispatch — `handle_request` is a 975-line chain of `if (request.method == "...")` blocks, of which `:2098–2574` (~476 lines) is the agent method family alone; (6) the run loop.

Because `Impl` is `.cpp`-private and reachable only through `ControlServer` RPC, none of these can be tested in isolation. That is the direct cause of the largest test file in the repo, which spawns real processes and sockets and reaches into implementation headers via `#include "../libs/draxul-server/src/..."`.

**Proposed boundary** — Three extractions inside `draxul-server`, no new library required initially:
- `ServerClientRegistry` (new `src/server_client_registry.{h,cpp}`): hello registration, token issue/verify, activity touch, prune, session association. Pure value logic over an injected `steady_clock` reading.
- `ServerSessionStore` (new `src/server_session_store.{h,cpp}`): the `ServerSession` record, checkpoint task scheduling and result collection, restore-warning accumulation.
- `ServerRequestRouter` (new `src/server_request_router.{h,cpp}`): a `std::string_view → handler` table replacing the if-chain, with per-domain handler objects. Split the agent branch into `ServerAgentCommandService` sitting beside the existing read-only `ServerAgentService` (`libs/draxul-server/include/draxul/server_agent_service.h:33`), which already owns `handle(method, params)` for the query methods.

**Dependency shape** — All three stay private to `draxul-server` (`src/`, quoted includes). Callers: `ServerKernel::Impl` only. Dependencies: `draxul-control` (for `ControlMethodResult`), `draxul-protocol`, `draxul-agent`. No new public headers; `include/draxul/server_kernel.h` (115 lines) is unchanged.

**Migration path** — (1) Add `draxul-server-test-internals` INTERFACE target exposing `libs/draxul-server/src` (mirrors `draxul-renderer-test-internals`, `tests/CMakeLists.txt:268`) and convert the five `../libs/...` includes in `server_kernel_tests.cpp:3–7`. (2) Move the client registry out first — it is self-contained and touches no session state. (3) Move `ServerSession` + checkpoint. (4) Introduce the router with the table initially populated by lambdas that call the existing `Impl` methods verbatim; move bodies one domain at a time. (5) Split the agent mutation family last, after `ServerAgentCommandService` exists.

**Testing improvement** — Token/limit/timeout/prune behavior, checkpoint scheduling and revision monotonicity, and per-method parameter validation become device-free, process-free unit tests. The 4928-line integration suite shrinks to genuine end-to-end cases (start/stop, real PTY, reconnect).

**Agent-work benefit** — Today any server change lands in one 3592-line TU with one 4928-line test file; two agents working on "client auth" and "checkpoint durability" collide on both. After the split there are four separately ownable files.

**Risks and prerequisites** — The kernel mutex currently protects everything; each extracted collaborator must document whether it is caller-locked (recommended: keep `Impl::mutex` as the sole lock, collaborators are non-locking). Windows/macOS parity risk is low (no platform code here), but `pending/09 macos-remote-terminal-channel -bug.md` and `pending/10 server-lifecycle-sigterm-eviction -bug.md` touch this file — sequence after those land to avoid conflicts.

---

## F2 — Three product hosts hand-roll the ImGui context/backend/font contract that `draxul-ui` already owns (P1)

**Location** — `libs/draxul-ui/src/ui_panel.cpp:194` (`CreateContext`), `:210–226` (`attach_imgui_backend`), `:229–252` (`detach`/`shutdown`), `:255–273` (`set_font`); contract documented at `libs/draxul-ui/include/draxul/ui_panel.h:84–110`. Duplicated in: `modules/satview/draxul-satview-host/src/satview_host.cpp:1026, 1169, 2183–2214`; `modules/megacity/draxul-megacity/src/megacity_host.cpp:530, 1072–1081, 1227–1228`; `modules/score/draxul-scoreview/src/score_host.cpp:273, 310–315, 1974–1999`.

**Current structural problem** — `kanban/done/29 gui-ui-contracts -refactor.md` designed and tested an explicit attach/detach lifetime contract (`tests/ui_panel_backend_tests.cpp`, tag `[imgui_backend]`), but only migrated the one caller that existed at the time (`app/diagnostics_panel_host.cpp:122–133`). The three product hosts each carry their own copy of the same protocol, and each copy is weaker than the tested one:

- `SatViewHost::attach_imgui_host` and `ScoreHost::attach_imgui_host` both discard the `bool` return of `IImGuiHost::initialize_imgui_backend()`; `MegaCityHost` does the same. `UiPanel::attach_imgui_backend` (`ui_panel.cpp:222`) treats failure as "nothing stays attached, return false".
- None of the three detach a previously-attached backend before overwriting `imgui_backend_ = &host`; `UiPanel` calls `detach_imgui_backend()` first, guaranteeing exactly one `shutdown_imgui_backend()` per attachment.
- `SatViewHost::set_imgui_font` (`:2195–2214`) and `ScoreHost::set_imgui_font` (`:1985–1999`) are line-for-line equivalent to `UiPanel::set_font`.

The reusable part of `UiPanel` is entangled with diagnostics-panel content (`update_diagnostic_state`, `compute_panel_layout`, terminal-cell metrics, fixed dockspace window names in `ui_panel.cpp:32–49`), which is presumably why nobody reused it.

**Proposed boundary** — Extract the lifetime half of `UiPanel` into `ImGuiHostContext` in `draxul-ui` (`include/draxul/imgui_host_context.h`, `src/imgui_host_context.cpp`). Public API, deliberately tiny: `bool initialize(ImGuiConfigOptions)`, `void shutdown()`, `bool attach_backend(IImGuiHost&)`, `void detach_backend()`, `void set_font(const std::string&, float)`, `ImGuiContext* context() const`, `IImGuiHost* backend() const`. `UiPanel` becomes a consumer of it; the diagnostics dockspace/window-name/layout logic stays in `UiPanel`.

**Dependency shape** — Callers: `UiPanel`, `SatViewHost`, `MegaCityHost`, `ScoreHost`, `DiagnosticsPanelHost`. Dependencies: `imgui` (PUBLIC, already `draxul-ui`'s PUBLIC dep — `libs/draxul-ui/CMakeLists.txt:19`) and the `IImGuiHost` contract from `draxul-host`. No SDL, no renderer, no product types. Implementation-private: nothing beyond the pimpl.

**Migration path** — (1) Add `ImGuiHostContext` and re-implement `UiPanel`'s four methods as forwarding calls; existing `[imgui_backend]` tests must pass unchanged. (2) Migrate `ScoreHost` (smallest, `imgui_context_`/`imgui_backend_`/`imgui_font_path_`/`imgui_font_size_pixels_` → one member). (3) `MegaCityHost`. (4) `SatViewHost` last — its `set_imgui_font` also triggers `refresh_scene_text_service()`, so the host keeps that side effect and delegates only the ImGui half.

**Testing improvement** — One tested implementation of attach/detach/re-attach/shutdown ordering covers four hosts. Failure of `initialize_imgui_backend()` becomes observable and testable per host instead of silently ignored in three of four.

**Agent-work benefit** — Any future ImGui version bump or font-atlas change is a one-file edit in `draxul-ui` instead of four coordinated edits across three optional product modules that build under different CMake flags.

**Risks and prerequisites** — Real behavior change: hosts start honoring `initialize_imgui_backend()` failure. Verify on both backends — `IImGuiHost` is implemented per renderer, so run `--host satview`, `--host megacity`, `--host score` once on Vulkan and once on Metal. `DRAXUL_ENABLE_*=OFF` configurations must still build (`draxul-ui` has no product dependency, so this is safe by construction).

---

## F3 — SatView view settings exist in four places with clamping duplicated in three (P1)

**Location** — `modules/satview/draxul-satview-core/include/draxul/satview/satview_config.h:140–192` (`SatViewConfig`, 47 fields + 24 range constants); `modules/satview/draxul-satview-host/include/draxul/satview/satview_host.h:204–291` (~50 mirrored members out of ~110 total); `modules/satview/draxul-satview-host/src/satview_host.cpp:2251–2303` (`current_config`, 49 assignments) and `:2305–2402` (`apply_config`, 95 lines); `modules/satview/draxul-satview-host/src/satview_config_io.cpp:193–211`; ImGui slider ranges at `satview_host.cpp:3787, 3799, 3914, 4333`.

**Current structural problem** — Adding one SatView setting requires coordinated edits in six locations: the `SatViewConfig` field, its range constants, the TOML read in `satview_config_io.cpp`, the TOML write, a `SatViewHost` member, the `current_config()` line, the `apply_config()` line, and the ImGui control. Range enforcement is genuinely duplicated, not merely similar — `star_min_magnitude` is clamped to `[kMinimumStarMagnitude, kMaximumStarMagnitude]` in `satview_config_io.cpp:193`, again in `satview_host.cpp:2330–2333`, and again as slider bounds at `:3787`. `tone_map_exposure` and `milky_way_brightness` follow the same triple pattern. This is the single largest contributor to `SatViewHost`'s ~110 members and is a guaranteed merge conflict whenever two agents add unrelated settings.

**Proposed boundary** — Two narrow changes, neither of which requires a new target:
1. Add `SatViewConfig satview_normalize_config(SatViewConfig)` to `draxul-satview-core` — the *only* place clamps, cross-field repair (`star_min > star_max` swap, `camera_pov`→`central_body` selection, `projection_mode` fallback for non-Earth POV), and range constants are applied. `load_satview_config`/`store_satview_config` and the host both call it.
2. Replace the ~50 mirrored host members with a single `SatViewConfig settings_;` member. `current_config()` becomes `return settings_;`; `apply_config()` becomes `settings_ = satview_normalize_config(config);` plus the two genuine side effects (`rebuild_visible_stars()`, `update_constellation_line_styles()`, and the three ImGui text-buffer copies).

**Dependency shape** — `satview_normalize_config` lives in `draxul-satview-core` (already the owner of the config type and constants), depends only on `<algorithm>` and `satview_filter.h`. Callers: `satview_config_io.cpp`, `SatViewHost`. No new PUBLIC surface beyond one free function.

**Migration path** — Land `satview_normalize_config` first with characterization tests asserting it reproduces today's `apply_config` clamping exactly, then delete the clamps from `satview_config_io.cpp`, then swap the host members field-group by field-group (star/constellation group, tone-map group, ground group, surface-filter group) so each commit is small and independently green.

**Testing improvement** — Config normalization becomes a pure table-driven test in `draxul-test-satview` with no GPU, window, ImGui context, or clock — out-of-range TOML, cross-field repair, and round-trip idempotence (`normalize(normalize(x)) == normalize(x)`) all become cheap. Today these paths are only reachable through the offline host fixture (`tests/support/satview_host_fixture.h`).

**Agent-work benefit** — A settings change becomes a two-file edit (`satview_config.h` + one ImGui panel TU) instead of a six-site edit inside the repo's largest source file.

**Risks and prerequisites** — Complementary to, and smaller than, `kanban/pending/06 satview-scene-composer-host-split -refactor.md`; card 06 explicitly keeps "config/dirty state" orchestration in `SatViewHost` and targets the composer/controller/panel axis instead. This finding should land **before** card 06, because removing 50 members first makes the panel-TU moves in card 06 mechanical. Verify persisted `config.toml` round-trips byte-identically before/after. No Vulkan/Metal exposure.

---

## F4 — Product-host registration is duplicated between the executable and the test entrypoint (P1)

**Location** — `app/main.cpp:652–671` and `tests/test_main.cpp:55–75`. Also `CMakeLists.txt:381–392` and `tests/CMakeLists.txt:281–292` (parallel per-product link + `target_compile_definitions` blocks).

**Current structural problem** — The same seven registration calls, fenced by the same three `#ifdef DRAXUL_ENABLE_*` guards, exist in two translation units. They already differ: `main.cpp:655–659` registers `register_server_shell_host_metadata` conditionally on `shared_server`, while `test_main.cpp:61` registers it unconditionally. Adding a product host requires four coordinated edits (both mains, both CMake files), and a fifth if the root CMake sanitizer list at `CMakeLists.txt:186–247` needs the new target.

**Proposed boundary** — One function, `void register_product_host_providers(HostProviderRegistry&, ProductHostOptions)` where `ProductHostOptions { bool shared_server = false; }`, in a new minimal static library `draxul-product-hosts` (`app/product_hosts.{h,cpp}` or `libs/draxul-product-hosts/`). It is the single file carrying the `DRAXUL_ENABLE_MEGACITY/SATVIEW/SCOREVIEW` fences and the only compilation unit that links the optional product host targets.

**Dependency shape** — Callers: the `draxul` executable and `draxul-test-app`. Dependencies: `draxul-host` (registry), `draxul-markdown-host`, `draxul-kanban`, plus the three optional hosts under their flags. Public API: one function + one options struct. `draxul-app` still does not link it (preserving the invariant documented at `CMakeLists.txt:342–344`).

**Migration path** — Purely mechanical: create the target, move the block from `main.cpp`, delete the block from `test_main.cpp`, move the conditional `target_link_libraries`/`target_compile_definitions` from both CMake files into the new target's own `CMakeLists.txt`, and have `draxul`/`draxul-test-app` link it. No source behavior change except that `shared_server` gating becomes consistent between the two entrypoints (verify this is the intended behavior for tests first).

**Testing improvement** — Registration order and flag-conditional availability become directly testable (build the registry, assert the host-kind set) instead of only observable through `tests/host_provider_availability_tests.cpp` running against a globally-mutated singleton.

**Agent-work benefit** — Adding or removing a product host becomes a one-directory change. This is also the exact seam `kanban/ice-box/23 core-extra-repo-split -architecture.md` Phase 0 asks for (`register_extra_host_providers` guarded by `DRAXUL_HAVE_EXTRA_HOSTS`), but it is independently valuable and unblocked by the owner decisions that card is waiting on.

**Risks and prerequisites** — Confirm the `shared_server` divergence is a bug, not deliberate test behavior, before unifying. Configure and build with each of `DRAXUL_ENABLE_MEGACITY/SATVIEW/SCOREVIEW` set to OFF individually on both platforms.

---

## F5 — `draxul_main` is a 629-line executable-only function with no test seam (P1)

**Location** — `app/main.cpp:534–1162` (`draxul_main`), `:237–533` (`run_server_mode`, ~297 lines), `:1163–1172` (`WinMain`/`main`). `main.cpp` is compiled only into `add_executable(draxul ...)` at `CMakeLists.txt:349–352` — it is not part of `draxul-app`.

**Current structural problem** — `draxul_main` sequentially handles integration-CLI dispatch, control-CLI dispatch, argument parsing, console allocation policy (five separate `#ifdef _WIN32` blocks), macOS re-exec for server mode, host-provider registration, logging configuration, session subcommands (`list/rename/delete/delete-all/status/shutdown/force-stop`), screenshot mode, render-test mode, smoke mode, GUI-action mode, and finally normal launch. Because none of it lives in a library target, none of it is reachable from any test executable. `app/cli_args.cpp` *is* in `draxul-app` and *is* tested (`tests/cli_args_tests.cpp`), so the repo tests what the flags parse to but nothing about what the program then does with them.

**Proposed boundary** — Move mode dispatch into `draxul-app` as `int run_draxul(const ParsedArgs&, RuntimeEnvironment&)`, where `RuntimeEnvironment` bundles the injectable pieces already parameterized elsewhere (`executable_path`, `server_runtime_dir`, stdout/stderr sinks, an `AppDeps` factory — `app/app.h:62–85` already has the factory bundle). `main.cpp` keeps only: platform entry point, `command_line_args()`, Windows console/standard-stream plumbing (`:68–114`), signal/console-control handler installation (`:200–226`), and the call into `run_draxul`.

**Dependency shape** — `run_draxul` lives in `draxul-app` (`app/run_draxul.{h,cpp}`), reusing `AppDeps::from_options`. Server mode stays in `main.cpp` or moves alongside, since `draxul-server` is linked by the executable, not by `draxul-app` (`CMakeLists.txt:372–380`) — keep that boundary and pass a `std::function<int(const ParsedArgs&)> server_entry` into `RuntimeEnvironment` rather than adding a `draxul-app → draxul-server` edge.

**Migration path** — Extract mode-by-mode, starting with the session subcommands (pure client RPC + stdout, easiest to fake), then screenshot/render-test/smoke (already funnel into `App::run_screenshot`/`run_render_test`/`run_smoke_test`), then normal launch. Leave server mode and Windows console plumbing in `main.cpp` throughout.

**Testing improvement** — Mode selection precedence (integration CLI beats control CLI beats launch flags), exit codes for each subcommand, and error-message routing become unit-testable in `draxul-test-app` with a fake environment — none of which is testable today.

**Agent-work benefit** — CLI work stops requiring edits to a file that also owns platform entry points and process bootstrap, and it stops being invisible to the test suite.

**Risks and prerequisites** — Windows `WIN32_EXECUTABLE` console behavior is subtle (`ensure_console_io` is called at four different points depending on which parser recognized the args); preserve those call sites verbatim in `main.cpp` rather than moving them. Overlaps in spirit with `kanban/ice-box/23` Phase 0 but is orthogonal to it. Land after F4 so registration is already out of the way.

---

## F6 — Two libraries' private headers are reached by relative path instead of a declared test-internals target (P2)

**Location** — `tests/server_kernel_tests.cpp:3–7` (`#include "../libs/draxul-server/src/..."` ×5) and `tests/box_drawing_tests.cpp`, `font_tests.cpp`, `font_resolver_style_tests.cpp`, `font_style_model_tests.cpp`, `glyph_raster_error_tests.cpp` (`#include "../libs/draxul-font/src/..."` ×9). Compare `tests/CMakeLists.txt:268, 308–310, 316–319, 334` — five other targets use declared `*-test-internals` INTERFACE targets.

**Current structural problem** — The repo has an established, enforced pattern for reaching implementation headers (`draxul-renderer-test-internals`, `draxul-megacity-test-internals`, `draxul-codeviz-renderer-test-internals`, `draxul-satview-host-test-internals`, `draxul-scoreview-host-test-internals`, each an INTERFACE target declared beside the library it opens). `draxul-font` and `draxul-server` bypass it with `../` paths, so the CMake graph does not record that these tests depend on those libraries' internals, and moving either library's directory silently breaks tests with a preprocessor error rather than a CMake error.

**Proposed boundary** — Add `draxul-font-test-internals` in `libs/draxul-font/CMakeLists.txt` and `draxul-server-test-internals` in `libs/draxul-server/CMakeLists.txt`, each `INTERFACE`, each exposing only its own `src` directory and linking its owning library. Convert the 14 includes to `#include "font_resolver.h"` / `#include "topology_service.h"` form.

**Migration path** — Two independent commits, one per library; each is mechanical and verifiable by a single `draxul-test-core` build.

**Testing improvement / agent benefit** — Makes the internal-access surface of each library explicit and greppable. An agent asked "what may reach into `draxul-server/src`?" gets an answer from CMake instead of from a `grep` over test sources. Prerequisite for F1's test split.

**Risks** — None; INTERFACE targets add no compilation.

---

## F7 — `tests/support/` is a flat, globally-visible directory mixing core fakes with product fixtures (P2)

**Location** — `tests/CMakeLists.txt:9–14` (`draxul-test-support` INTERFACE exposes `tests/` and `tests/support/` to every focused target); `tests/support/` contains 27 headers, of which `satview_host_fixture.h` (273 lines), `scoreview_host_fixture.h` (356), `megacity_scene_test_support.h` (279), `scoreview_engrave_helpers.h`, `synthetic_piano.h`, and `wav_reader.h` are product-specific.

**Current structural problem** — Product fixtures that cannot compile outside their owning test target sit on the include path of `draxul-test-core` and `draxul-test-markdown-kanban`. It is benign at compile time (header-only, only included by owning tests) but it erases ownership: nothing in the build graph says `satview_host_fixture.h` belongs to SatView, and nothing prevents a core test from including it and producing a confusing link failure. It also means the "shared test support" boundary — which the CMake comment at `:6–8` explicitly frames as an anti-drift mechanism — silently spans four unrelated products.

**Proposed boundary** — Keep genuinely shared, dependency-free helpers in `tests/support/` (`fake_clock.h`, `temp_dir.h`, `scoped_env_var.h`, `home_dir_redirect.h`, `test_support.h`, `replay_fixture.h`, the `fake_*` host/renderer/window/grid seams). Move product fixtures to `tests/support/satview/`, `tests/support/scoreview/`, `tests/support/megacity/`, and add per-product INTERFACE targets (`draxul-test-support-satview`, …) that add only that subdirectory and link the corresponding `*-test-internals` target. Each product test target links its own; `draxul-test-core` links only the shared one.

**Migration path** — One product at a time; each move is an `#include` path update in the owning `*_tests.cpp` files plus one INTERFACE target.

**Testing improvement / agent benefit** — Fixture ownership becomes explicit and matches the already-modular test target layout. An agent working on SatView fixtures cannot accidentally widen the surface visible to core tests, and reviewers can see from CMake which fixtures a target may use.

**Risks** — None structural. `tests/CMakeLists.txt:130–204` statically fingerprints test-case and tag inventories; moving *support headers* does not touch `*_tests.cpp` files, so the baselines at `:186–190` are unaffected.

---

## F8 — The render-scenario manifest is validated twice, in two languages, with divergent rules (P2)

**Location** — `CMakeLists.txt:580–655` (~75 lines of `string(JSON ...)` parsing and five `FATAL_ERROR` checks) versus `do.py:658–730` (`load_render_manifest`, ~90 lines, covered by `tests/do_py_tests.py` via the `draxul-do-py-tests` CTest entry at `tests/CMakeLists.txt:357–366`). Both read `tests/render/manifest.json`.

**Current structural problem** — Neither validator is a superset of the other. CMake alone checks that each scenario's `.toml` exists, that required per-platform reference BMPs exist, and that no unregistered `.toml` or `.bmp` is present. `do.py` alone checks the field schema, the `status` and `platforms` enums, boolean types, `ctest ⇒ reference_required`, `renderall == ctest`, `blessall ⇒ reference_required`, and duplicate command names. An agent adding a scenario must therefore satisfy two independently-maintained rule sets, and a missing reference BMP fails at **CMake configure** time — breaking configure for every unrelated task in the tree until fixed.

**Proposed boundary** — Make `do.py` the single schema authority (it is already the tested one) and reduce the CMake side to consumption. Concretely: extract the CMake block into `cmake/RenderScenarioTests.cmake` as `draxul_register_render_scenario_tests()`, and have it read a generated list rather than re-parse and re-validate JSON — or, if a build-time Python dependency is undesirable, keep the CMake parse but move every *validation* rule to `do.py hygiene` and leave CMake with `add_test` registration plus non-fatal `message(WARNING)` for a missing reference.

**Dependency shape** — `cmake/RenderScenarioTests.cmake` joins the existing `cmake/` module set (`CheckDependencyBoundaries.cmake`, `CompileShaders*.cmake`, `FetchDependencies.cmake`). Root `CMakeLists.txt` shrinks by ~75 lines.

**Migration path** — Step 1 is pure relocation into `cmake/` with no rule change (safe, immediately reduces the 660-line root file). Step 2 moves the validation rules and downgrades the configure-time fatals.

**Testing improvement / agent benefit** — One place to learn the manifest contract; `python -m unittest tests.do_py_tests` becomes the complete check. A malformed scenario stops being able to block configure for the whole repository.

**Risks** — Keep the `RESOURCE_LOCK draxul_gpu` and `TIMEOUT 30` properties (`CMakeLists.txt:631–632`) intact; they serialize GPU tests. Re-verify scenario registration on both platforms since `DRAXUL_RENDER_PLATFORM` gating lives in the moved block.

---

## F9 — `app/session_state.h` is a compatibility shim that drags the whole GUI graph into the tab/space model (P2)

**Location** — `app/session_state.h` (9 lines: forwards to `<draxul/session_state.h>` *and* includes `"pane_manager.h"`); consumers at `app/app.h:11`, `app/tab_controller.h:3`, `app/session_id.cpp:3`, `tests/app_smoke_tests.cpp:16`, `tests/session_state_tests.cpp:2`. Chain: `space_controller.h` → `space.h` → `tab_controller.h` → `session_state.h` + `tab.h` → `pane_manager.h` → `<draxul/host.h>`, `IGridRenderer`, `TextService`, `IImGuiHost`.

**Current structural problem** — `SpaceController` and `TabController` read as clean, dependency-light collaborators (their own `.cpp` files include only `<draxul/log.h>` and standard headers), but their headers transitively pull the entire host/renderer/font surface because of the shim. The comment in `session_state.h` calls this "preserve the former transitive app types while callers migrate" — the migration has not happened, and the shim now guarantees it cannot be observed as incomplete. Combined with `target_include_directories(draxul-app PUBLIC app)` (`CMakeLists.txt:316`), all 38 `app/*.h` headers are a public surface for anything linking `draxul-app`, so there is no compiler-enforced distinction between app-internal and app-public headers anywhere in the app layer.

**Proposed boundary** — Two decoupled steps:
1. Delete the `#include "pane_manager.h"` line from `app/session_state.h` and add the include explicitly at the four sites that actually need `PaneManager` types. This is a one-line change whose fallout is exactly the hidden coupling it currently masks.
2. Split `draxul-app`'s include surface: keep `target_include_directories(draxul-app PUBLIC app)` for now, but introduce `app/include/draxul/app/` for the headers tests and `main.cpp` legitimately need (`app.h`, `cli_args.h`, `control_cli.h`, `session_id.h`) and move the rest behind a PRIVATE `app` include directory as part of card 01's CMake relocation.

**Dependency shape** — Step 1 has no target changes at all. Step 2 rides on `kanban/pending/01 app-local-cmake-ownership -refactor.md`, which already creates `app/CMakeLists.txt`; adding the PUBLIC/PRIVATE split there costs one extra `target_include_directories` line and no source moves beyond the four public headers.

**Testing improvement / agent benefit** — Once step 1 lands, `tab_controller.h` and `space_controller.h` compile without the renderer graph, so `tests/space_controller_tests.cpp` (917 lines) and the tab tests can eventually move from `draxul-test-app` to a lighter target. It also makes `kanban/ice-box/22 app-tab-session-controllers -refactor.md` materially easier, since its `SessionProjectionController` extraction currently inherits everything through this shim.

**Risks** — Step 1 may surface a handful of missing includes; that is the point. Purely compile-time, no platform exposure.

---

## F10 — The dependency-boundary checker guards foundations but not the app or renderer layers (P2)

**Location** — `cmake/CheckDependencyBoundaries.cmake:21–92`, invoked at `CMakeLists.txt:182–183`. The `_product_target_pattern` loop (`:71–92`) covers exactly ten foundation targets: `draxul-performance`, `draxul-host-identity`, `draxul-types`, `draxul-bmp`, `draxul-terminal-core`, `draxul-terminal-process`, `draxul-session-model`, `draxul-protocol`, `draxul-client`, `draxul-server`.

**Current structural problem** — This is one of the repo's best structural assets and it stops short of the layers most likely to regress. `draxul-app`, `draxul-renderer`, `draxul-host`, `draxul-ui`, `draxul-gui`, `draxul-config`, and `draxul-runtime-support` have no product-leak guard. The most important invariant in the app layer — "`draxul-megacity` is intentionally NOT linked here; the terminal product library has zero megacity dependency" — exists only as a prose comment at `CMakeLists.txt:342–344`. Nothing fails if someone adds the link.

**Proposed boundary** — Extend `draxul_check_dependency_boundaries()` with:
```
draxul_reject_direct_links(draxul-app draxul-megacity draxul-satview-host draxul-scoreview-host)
draxul_reject_direct_links(draxul-renderer draxul-host draxul-config draxul-window)  # verify against current links first
draxul_reject_direct_links(draxul-ui draxul-host)   # draxul-ui already avoids this
```
plus adding `draxul-app`, `draxul-renderer`, `draxul-host`, `draxul-ui`, `draxul-gui`, `draxul-runtime-support` to the existing `_product_target_pattern` foundation loop, and `draxul_check_direct_link(draxul-app draxul-host)` for the edges the app genuinely owns.

**Migration path** — Add one assertion at a time, configure, keep the ones that pass, and record any that fail as separate findings rather than force-fixing them in the same commit. The mechanism and both helper functions already exist; this is additive configure-time logic with zero build cost.

**Testing improvement / agent benefit** — Converts the strongest architectural invariants in the tree from comments into configure failures. Directly serves parallel-agent safety: an agent adding a product dependency in the wrong layer learns at configure time, not at review time.

**Risks and prerequisites** — Sequence after `kanban/pending/00 internal-target-build-policy -refactor.md`, since that card also touches root-level target enumeration and would otherwise conflict. Must be verified with each `DRAXUL_ENABLE_*` flag OFF, because `draxul_reject_direct_links` on a nonexistent target name is a silent no-op rather than an error — worth guarding with `if(TARGET ...)`.

---

## F11 — `draxul-test-core` carries ~97 sources and 18 library links as the unclassified default (P2)

**Location** — `tests/CMakeLists.txt:110–111` (core = everything unclassified), `:247–272` (target + 18 `target_link_libraries` entries + 4 shards). Of 202 `*_tests.cpp` files, 33 are classified to app and 72 to product modules, leaving roughly 97 in core, including the four heaviest non-app suites: `server_kernel_tests.cpp` (4928), `remote_terminal_host_tests.cpp` (1886), `control_plane_tests.cpp` (1371), `terminal_vt_tests.cpp` (1310).

**Current structural problem** — The fallback rule itself is sound and deliberately documented (`:16–19`): unclassified tests land in core so that a test needing an app or product dependency fails to link until it is classified. The problem is core's *size and fan-out*. Any change to `draxul-server`, `draxul-client`, `draxul-nanovg`, `draxul-renderer`, or `draxul-host` relinks a 97-source binary, and the four shards run every one of those suites regardless of what changed. There is no narrower target an agent working on the terminal VT layer or the server can build.

**Proposed boundary** — Two additional focused targets using the existing `draxul_add_test_target()` helper (`tests/CMakeLists.txt:211–245`), which already handles PCH, shards, labels, sanitizers, and the `draxul-tests` aggregate:
- `draxul-test-server` (label `server`): `server_kernel_tests.cpp`, `control_plane_tests.cpp`, `remote_terminal_*_tests.cpp`, topology/protocol/client suites. Links `draxul-server`, `draxul-client`, `draxul-protocol`, `draxul-control`, `draxul-terminal-process`, `draxul-server-test-internals` (F6) — and notably *not* `draxul-renderer`, `draxul-nanovg`, `draxul-gui`, `draxul-window`.
- `draxul-test-terminal` (label `terminal`): `terminal_vt_tests.cpp` and the VT/SGR/scrollback/selection suites, linking `draxul-terminal-core` + `draxul-grid` only.

Core keeps the fallback role for genuinely cross-cutting suites.

**Migration path** — Add one target at a time. The static partition guard at `:95–128` and the case/tag inventory fingerprint at `:130–204` already prove no test is lost or duplicated across the move, so each step is self-verifying at configure time.

**Testing improvement / agent benefit** — A server-layer agent builds and runs a target that links six libraries instead of eighteen, and a terminal-core agent builds one that links two. Combined with the `do.py test --label <label>` entry point that `kanban/pending/08` will add, this is what makes narrow validation actually narrow.

**Risks and prerequisites** — Overlaps in intent with `kanban/pending/02 host-layer-static-libraries -refactor.md` and `03 nvim-protocol-transport-boundaries -refactor.md`, both of which *retarget* test linkage as libraries split. Neither card creates new test executables, so this is complementary — but it should land **after** cards 02/03 so the new targets are defined against final library names rather than being rewritten twice. `DRAXUL_TEST_SOURCE_BASELINE`/`REGISTRATION_BASELINE` at `:186–190` need no change (they count sources, not targets).

---

## F12 — `draxul-app-support` is an empty aggregate with a misleading name (P2)

**Location** — `libs/draxul-app-support/CMakeLists.txt` (7 lines, entire library: `add_library(... INTERFACE)` + three links to `draxul-config`, `draxul-runtime-support`, `draxul-render-test`). Directory contains no headers or sources. Sole consumer: `tests/CMakeLists.txt:251`.

**Current structural problem** — A `libs/` entry with a name suggesting it owns app-support code owns nothing; it is a three-item link alias used by exactly one target. It occupies a slot in the library listing at `CMakeLists.txt:168` and in `docs/module-map.md`'s library inventory, costing every agent who reads the module map a lookup that yields nothing.

**Proposed boundary** — Either inline the three links directly into `draxul-test-core` and delete the directory, or — if a named bundle is genuinely wanted — rename it to something that says what it is (`draxul-app-runtime-deps`) and add a one-line comment stating that it is deliberately content-free. Recommendation: inline and delete; `draxul-test-core` already lists 18 links, so three more is not a legibility loss.

**Migration path** — One commit: three lines moved, one `add_subdirectory` removed, one directory deleted, one line removed from `docs/module-map.md`.

**Agent benefit** — Removes a false signal from the library inventory. Trivial, but it is exactly the kind of noise that costs context on every module-map read.

**Risks** — None. Verify nothing else references the name (`grep` confirms only the two sites above).

---

## Proposed module / target map

Changes only; everything not listed is unchanged.

```
libs/draxul-ui
  + ImGuiHostContext            (F2) public: include/draxul/imgui_host_context.h
                                     consumers: UiPanel, SatViewHost, MegaCityHost,
                                                ScoreHost, DiagnosticsPanelHost

libs/draxul-server                (all private, src/ only)
  + ServerClientRegistry        (F1)
  + ServerSessionStore          (F1)
  + ServerRequestRouter         (F1)
  + ServerAgentCommandService   (F1) beside existing ServerAgentService
  + draxul-server-test-internals (F6) INTERFACE

libs/draxul-font
  + draxul-font-test-internals  (F6) INTERFACE

libs/draxul-satview-core
  + satview_normalize_config()  (F3) sole owner of clamps + cross-field repair

app/ (or libs/draxul-product-hosts)
  + draxul-product-hosts        (F4) register_product_host_providers()
                                     linked by: draxul, draxul-test-app
  + run_draxul()                (F5) in draxul-app; main.cpp keeps platform entry only

cmake/
  + RenderScenarioTests.cmake   (F8) moved out of root CMakeLists.txt
  ~ CheckDependencyBoundaries   (F10) extended to app/renderer/host/ui layers

tests/
  + draxul-test-server          (F11) label "server"
  + draxul-test-terminal        (F11) label "terminal"
  ~ tests/support/{satview,scoreview,megacity}/ (F7) per-product INTERFACE targets
  - draxul-app-support          (F12) inlined into draxul-test-core
```

---

## Best isolated work packages for parallel agents

Ordered by readiness. Packages within a group have no file overlap and can run concurrently.

**Group A — no prerequisites, fully independent**
1. **F6** — test-internals targets for `draxul-font` and `draxul-server`. Two CMake files, 14 include lines. Prerequisite for F1 and F11.
2. **F12** — delete `draxul-app-support`. One directory, three lines.
3. **F9 step 1** — remove `#include "pane_manager.h"` from `app/session_state.h`, fix fallout at four call sites.
4. **F8 step 1** — relocate the render-manifest block to `cmake/RenderScenarioTests.cmake` with no rule change.

**Group B — one prerequisite each, still mutually independent**
5. **F2** — `ImGuiHostContext`. Touches `draxul-ui` + three product hosts; no overlap with any other package. Highest cross-module value per line changed.
6. **F3** — SatView config consolidation. Touches only `modules/satview/`. Should land before pending card 06; a single SatView owner should hold both.
7. **F4** — product-host registration seam. Touches `main.cpp`, `test_main.cpp`, two CMake files.
8. **F10** — extend `CheckDependencyBoundaries.cmake`. Sequence after pending card 00.

**Group C — sequenced**
9. **F1** — server kernel decomposition. Needs F6. Single owner, four sub-commits (client registry → session store → router → agent commands). Sequence after `pending/09` and `pending/10` bug fixes to avoid conflicts in the same TU.
10. **F11** — `draxul-test-server` / `draxul-test-terminal`. Needs F6; sequence after pending cards 02 and 03 so target names are final.
11. **F5** — `run_draxul` extraction. Sequence after F4.
12. **F7** — test-fixture ownership split. Best done per-product by the same owners doing F3 / card 05 / card 07.

---

## Strongest existing structural qualities worth preserving

These are unusually good and should be defended in any refactor:

- **`cmake/CheckDependencyBoundaries.cmake`** — configure-time enforcement of both required edges (`draxul_check_direct_link`) and forbidden edges (`draxul_reject_direct_links`), plus a product-leak regex over the ten foundation targets. Very few C++ repos enforce layering mechanically at all. F10 extends it rather than replacing it.
- **Link-isolation executables** (`tests/CMakeLists.txt:396–432`) — eight one-TU builds proving each public header compiles and links through its owning target alone. This is the mechanism that keeps transitive-include rot from accumulating.
- **The focused test-target architecture** (`tests/CMakeLists.txt:95–204`) — the partition proof, the static case/tag inventory fingerprint, and the regression baselines together make it structurally impossible to lose or duplicate a test while moving targets. This is the single most valuable asset for safe parallel agent work in the repo.
- **`*-test-internals` INTERFACE targets** — the right way to open implementation headers to tests without widening the public surface. F6 exists only because two libraries missed the pattern.
- **`modules/megacity/`** — the reference decomposition: seven cohesive libraries with an explicit dependency-direction diagram in its `AGENTS.md`, and a host further split into `megacity_host_panels`, `megacity_camera_input`, `scene_publication`, `scene_snapshot_builder`, `city_picking`, `city_selection`, `metrics_overlay_controller`. It is the model SatView should follow (and which pending card 06 correctly points at).
- **`libs/draxul-config/src/config_schema.cpp`** — a 54-row declarative descriptor table with captureless-lambda accessors and defaults read back from a default-constructed `AppConfig`, feeding queries, generated docs, and the parse/serialize driver from one source. F3 proposes SatView move toward this.
- **The renderer backend split** (`libs/draxul-renderer/CMakeLists.txt`) — OBJECT libraries per backend behind one facade target, with `PUBLIC include` / `PRIVATE src` on every target and zero backend types in public headers.
- **`AppDeps` / `SatViewHost::TestHooks`** — explicit, documented, production-inert injection seams (`app/app.h:62–85`, `satview_host.h:110–125`) that let hosts initialize and pump with no GPU, network, or system clock.

---

## Appendix — tracked items confirmed still open (not new findings)

Listed only to inform sequencing; each is already owned by a kanban card and is excluded from the findings above.

- `pending/06 satview-scene-composer-host-split` — confirmed: `satview_host.cpp` is 5317 lines with `render_control_panel` alone spanning `:3943–4759`. F3 should land first to make card 06's panel moves mechanical.
- `pending/08 agent-guidance-label-validation` — confirmed stale guidance: `CLAUDE.md:127–130` documents `IBaseRenderer → I3DRenderer → IGridRenderer`, `IHost → I3DHost → IGridHost`, and `attach_3d_renderer()`, none of which exist in `libs/draxul-host/include/` or `libs/draxul-renderer/include/` today; `docs/module-map.md:140,149` repeats the renderer claim. `CLAUDE.md:59,61` and `modules/megacity/AGENTS.md` instruct running `.\build\Release\draxul-tests.exe` and `ctest -R draxul-tests`, but `draxul-tests` is an `add_custom_target` (`tests/CMakeLists.txt:209`) with no executable; the megacity guide also names `megacity_scene_tests.cpp`, which has been split into `megacity_scene_{world,host,layout}_tests.cpp`. No `modules/satview/AGENTS.md` or `modules/score/AGENTS.md` exists. `scripts/run_tests.{sh,bat}` always build the full aggregate and run every `unit`-labeled test, so the per-module labels defined in CMake (`core`, `app`, `markdown`, `kanban`, `megacity`, `satview`, `scoreview`, `scoreview-host`) have no tooling entry point.
- `pending/00`, `01`, `02`, `03`, `04`, `05`, `07` — confirmed accurate against the current tree; F10 and F11 are explicitly sequenced behind them.
- `ice-box/22 app-tab-session-controllers` — confirmed: `App` still has ~60 members (`app/app.h:268–372`) with the remote-topology projection block spanning `app.cpp:3020–4230`. `TabController` and `SpaceController` already exist, so this card is partially delivered; F9 reduces its remaining difficulty.
- `ice-box/125 overlay-registry-refactor` — confirmed: the four overlay hosts are still individually wired through `initialize`, render-tree assembly, viewport update, and `shutdown`.
- `ice-box/23 core-extra-repo-split` Phase 0 — F4 delivers the registration seam this card asks for, without depending on its open owner decisions.
