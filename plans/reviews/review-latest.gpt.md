# Draxul Repository Review

## Review scope

This was a static, read-only review of the repository at:

- Branch: `codex/satview-ground-projections`
- Commit: `7da5dc48` — `Review update`
- Snapshot time: 2026-07-05 11:05 BST
- In-scope inventory: 562 files returned by `rg --files` under `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, and `plans/`
- Text inspected: 552 files
- Binary render references inventoried: 10 BMP files
- Additional architectural context inspected: 158 files under `modules/`, root CMake files, presets, documentation, and GitHub workflows

Every relevant text file was read from disk directly. Pre-generated combined source files were not used. Historical reviews were treated only as history; findings below were checked against the current tree.

No build or tests were run because the instruction was review-only. No repository files were edited.

### Moving-worktree note

The working tree changed during the review. A concurrent implementation added shared text-atlas code and SatView constellation-boundary catalog work. The final scan includes those files. These changes were not made by this review.

The current uncommitted work includes:

- A shared `TextAtlas` and `build_text_atlas()` extraction.
- MegaCity migration to that shared atlas.
- A versioned SatView constellation-boundary catalog and generator.
- New catalog and atlas tests.
- An active SatView observatory/boundary implementation plan.

That work is clearly still in progress, so transient observations about it are separated from the committed architectural findings.

## Executive assessment

Draxul is substantially better structured and better tested than its largest files initially suggest. The library boundaries, cross-platform renderer design, deterministic parser tests, render snapshots, and typed host/render abstractions show considerable engineering discipline.

The principal weakness is that the integration layer has grown faster than the architecture documents and enforcement mechanisms:

- `App`, `ChromeHost`, `main.cpp`, session attach, and product renderers are now major merge hotspots.
- Architectural guidance describes types that no longer exist.
- Important completed test work is silently no longer registered.
- Optional module isolation is incomplete on Windows.
- All CI workflows are manually triggered.
- Several developer automations disagree about the canonical planning layout or grant write-capable tools to unattended review agents.
- The weather service turns local configuration into a shell command without complete escaping.

The most urgent work is not another large refactor. It is a short reliability pass:

1. Remove shell command construction from network services.
2. Fix the two new unsafe post-`fork()` launch paths on macOS.
3. Make CI and render-test registration fail loudly.
4. Make session-state writes atomic and restore macOS attach-test coverage.
5. Refresh agent-facing architectural documentation.
6. Then decompose the major integration hotspots along already-visible responsibility boundaries.

## Current architecture

| Layer | Main responsibilities | Assessment |
|---|---|---|
| `draxul-types`, `draxul-config` | Shared types, errors, logging, configuration | Clean and broadly reusable |
| `draxul-window`, `draxul-font`, `draxul-grid`, `draxul-nvim` | Platform windowing, text, grid state, RPC | Generally cohesive and well tested |
| `draxul-renderer`, `draxul-nanovg` | Frame context, grids, render passes, Vulkan/Metal backends | Good public model; backend implementations are becoming very large |
| `draxul-host` | Terminal hosts, VT behavior, host lifecycle | Strong shared base implementation; `IHost` is becoming a broad capability surface |
| `app/` | Composition, sessions, chrome, input, command routing | Too many integration concerns converge here |
| Markdown/Kanban modules | Product hosts with mostly isolated models | Good examples of product-level modularity |
| MegaCity/SatView modules | Rich data, UI, simulation, and dual-backend rendering | Valuable separation from terminal code, but internally monolithic |
| Tests and scripts | Broad unit/integration coverage and workflow automation | Excellent test volume; registration and automation contracts have drifted |

The actual renderer model is now effectively:

```text
IBaseRenderer
└── IGridRenderer

IFrameContext
└── records one or more IRenderPass objects for the current frame
```

The actual host model is a flat `IHost` interface, with `GridHostBase` providing shared grid behavior. There is no current `I3DRenderer`, `I3DHost`, or `IGridHost` hierarchy.

## High-priority findings

### 1. `weather_location` permits shell-command injection

Severity: **High**

The config loader accepts an arbitrary string for `weather_location` in [app_config_io.cpp](/D:/dev/Draxul/libs/draxul-config/src/app_config_io.cpp:543). The geocoder performs only a partial URL encoding in [weather_service.cpp](/D:/dev/Draxul/app/weather_service.cpp:210), then wraps the resulting URL in double quotes and passes it through `_popen`/`popen` in [weather_service.cpp](/D:/dev/Draxul/app/weather_service.cpp:360).

Characters such as `"`, `$`, backticks, backslashes, and platform shell metacharacters are not safely handled. A malicious or accidentally hostile local config value can terminate the quoted URL and execute shell syntax.

Related weaknesses:

- JSON is parsed with `find`, `substr`, and `strtod`.
- Country extraction assumes a particular whitespace layout after `:`, so minified JSON can be misread.
- Missing numeric keys become zero while the request may still be reported as successful.
- `stop()` waits for the worker thread, which may be blocked in `curl` for up to the configured timeout.
- SatView catalog and cloud services implement separate shell-based `curl` wrappers with similar cancellation limitations.

Recommended direction:

- Introduce one injected, cancellable HTTP client in a low-level library.
- Use an HTTP library or an argv-based process API without an intervening shell.
- Use complete RFC 3986 query encoding.
- Parse JSON with a real parser.
- Give requests explicit cancellation and bounded shutdown semantics.
- Keep service-specific parsing and caching above the transport abstraction.

### 2. Two macOS launch paths allocate C++ objects after `fork()`

Severity: **High on macOS**

The lower-level Neovim and PTY launchers prepare strings and vectors before `fork()`, which is the correct pattern.

Two newer app paths do not:

- [session_picker_host.cpp](/D:/dev/Draxul/app/session_picker_host.cpp:286) constructs a `std::vector<char*>` and calls `executable_path_.string()` in the child.
- [main.cpp](/D:/dev/Draxul/app/main.cpp:364) copies a `std::vector<std::string>`, allocates another vector, and calls `exe_path.string()` in the child.

After `fork()` in a multithreaded process, heap allocation is unsafe because the child can inherit the allocator lock held by a vanished thread. The session picker is particularly exposed because it runs after application threads have started.

Use `posix_spawn()` for these simple self-launches, or prepare every byte and pointer in the parent and restrict the child to async-signal-safe operations before `execve()`.

This is a regression variant of an older completed fork-safety item, not a duplicate of the already-fixed Neovim/PTY work.

### 3. There is no automatic push or pull-request CI

Severity: **High process risk**

Every workflow under `.github/workflows` uses only `workflow_dispatch`; for example [build.yml](/D:/dev/Draxul/.github/workflows/build.yml:3). The documentation accurately describes the workflows as manual, but that means none of the repository’s substantial validation matrix protects normal changes automatically.

The format job also scans only `app`, `libs`, and `tests` in [format.yml](/D:/dev/Draxul/.github/workflows/format.yml:22). It excludes the large and fast-changing `modules/` tree.

This is especially risky because:

- Windows and Metal implementations can drift independently.
- Render scenarios can disappear silently.
- Many agents may edit the same checkout concurrently.
- The active shared-atlas work crosses types, font, MegaCity, SatView, tests, scripts, and assets.

A small required PR matrix would provide much more value than running every expensive workflow:

- Windows release compile plus unit tests.
- macOS release compile plus unit tests.
- Formatting across `app`, `libs`, `modules`, and tests.
- Optional-module configuration checks.
- Render-manifest integrity.
- Scheduled or manually invoked sanitizers and full snapshots.

The existing completed CI item promised push/PR enforcement, so this should be treated as a completion regression.

### 4. Render-test registration silently overstates coverage

Severity: **High confidence risk**

The CMake list in [CMakeLists.txt](/D:/dev/Draxul/CMakeLists.txt:426) names:

- `basic-view`
- `cmdline-view`
- `unicode-view`
- `panel-view`
- `ligatures-view`

The current files and references yield:

| Scenario | TOML | Windows/macOS references | Registered result |
|---|---:|---:|---|
| `basic-view` | Yes | Yes | Runs |
| `cmdline-view` | Yes | Yes | Runs |
| `unicode-view` | Yes | Yes | Runs |
| `panel-view` | Yes | Yes | Runs |
| `ligatures-view` | No | No | Silently skipped |
| `wide-char-scroll` | Yes | No | Not registered |
| `nanovg-demo` | Yes | Yes | Not registered |

The completed ligature and wide-character work items both claim render coverage. Current CMake silently converts missing files into status messages. Documentation still lists six built-in scenarios in [features.md](/D:/dev/Draxul/docs/features.md:439), but only four are currently runnable through CTest.

A single manifest should define scenario name, supported platforms, reference requirements, and whether absence is allowed. Missing required assets should fail configuration or a repository-integrity test.

### 5. Session persistence is non-atomic, while macOS excludes all attach tests

Severity: **High data and platform risk**

Session state and runtime metadata are written directly to their final files using `std::ios::trunc` in [session_state.cpp](/D:/dev/Draxul/app/session_state.cpp:496). A crash, power loss, or disk failure can replace a valid session with an empty or partial TOML file.

The Kanban store and SatView cache already contain temp-file/replace patterns that could be reused.

Separately, [tests/CMakeLists.txt](/D:/dev/Draxul/tests/CMakeLists.txt:55) removes `session_attach_tests.cpp` entirely on Apple because of a Catch2 `__int128` formatting conflict. This excludes the platform where the session IPC path is most different. The omitted file contains both protocol and application-level persistent-session behavior.

The 1,385-line [session_attach.cpp](/D:/dev/Draxul/libs/draxul-runtime-support/src/session_attach.cpp:699) also combines:

- Windows SID, DACL, named-pipe, and integrity-label code.
- POSIX socket code.
- Shared protocol serialization.
- Server lifecycle and client commands.

Recommended split:

- Shared protocol and framing.
- `session_attach_win.cpp`.
- `session_attach_posix.cpp`.
- Small server/client orchestration layer.
- Atomic session state writer with backup or corrupt-file quarantine.

### 6. Windows optional-module builds still compile every module shader

Severity: **Medium-high**

The source-level CMake gates for MegaCity and SatView are mostly clean. On Windows, however, [CompileShaders.cmake](/D:/dev/Draxul/cmake/CompileShaders.cmake:11) globs every root `.vert` and `.frag` file. The executable always depends on `compile_shaders`.

Therefore, disabling MegaCity or SatView still discovers and compiles their Vulkan shaders. This contradicts the feature documentation’s stronger claim that disabled modules remove their shader/build coupling.

The Metal path already gates module shader compilation explicitly, so the two platforms have different optionality semantics.

The active SatView plan already includes optional-off validation, so no duplicate work item is needed. The structural fix is to give each module its own shader target and attach that target only when the module is enabled.

### 7. Render-test code remains coupled to production targets

Severity: **Medium**

`draxul-render-test` is always added and `draxul-app` publicly links it. `app.cpp` and `main.cpp` unconditionally include and call render-test types; the CMake option mainly changes definitions and behavior rather than removing the dependency.

The NanoVG demo host is also registered unconditionally in [main.cpp](/D:/dev/Draxul/app/main.cpp:517), despite being implemented inside `draxul-render-test` and omitted from the documented user host list.

This is another regression from the completed render-test extraction work. A cleaner shape would be:

- Production app library without snapshot parsing/comparison.
- A render-test executable or test-only adapter that composes the production app.
- NanoVG demo registration only in render-test builds.
- A separate screenshot capability if screenshots are a supported product feature.

## Maintainability and modularity

### 8. `App` remains the principal integration god object

[app.cpp](/D:/dev/Draxul/app/app.cpp:263) is approximately 2,870 lines. It owns or coordinates:

- Window and renderer startup.
- Text service initialization.
- Config loading and reload.
- Workspace and host lifecycle.
- Chrome, command palette, diagnostics, and toasts.
- GUI action wiring.
- Session save/load, attach, detach, rename, and metadata.
- Render-test execution.
- Event-loop scheduling.
- System resource and weather integration.

Large methods include initialization, chrome setup, action wiring, session load/save, and the main pump path.

The most natural extractions are already visible:

- `ApplicationBootstrap`
- `WorkspaceController`
- `SessionController`
- `OverlayController`
- `FrameScheduler`
- `GuiActionRouter`

These should be behavioral collaborators with focused tests, not merely file-level helper functions.

### 9. `ChromeHost` mixes layout, vector drawing, grid text, editing, and hit-testing

[chrome_host.cpp](/D:/dev/Draxul/app/chrome_host.cpp:325) is about 1,500 lines, and `draw()` accounts for roughly 640 lines.

It simultaneously handles:

- Tab layout.
- Pane divider and focus geometry.
- NanoVG drawing.
- Grid-backed text overlay.
- Weather pill layout.
- Rename editing and caret behavior.
- Hit-testing.
- DPI and physical/logical/cell coordinate conversion.

Its header says it owns workspaces, but `App` owns them and passes read-only pointers. Its `Deps` also contains several apparently unused fields, including options, config document, window, ImGui host, display PPI, owner lifetime, and viewport computation.

Suggested split:

- Pure `ChromeLayout` producing geometry and hit regions.
- `ChromeVectorPass` for NanoVG shapes.
- `ChromeTextLayer` for grid text.
- `RenameEditor`.
- A much smaller `ChromeHost` that combines the outputs.

A pure layout model would make DPI and tab behavior easier to test without a renderer.

### 10. Input routing remains a large stateful intersection

`InputDispatcher` is about 923 lines. Although the newer `IInputRouter` is a real improvement, the dispatcher still mixes:

- Chord state and keybinding dispatch.
- ImGui capture.
- Overlay interception.
- Tab and pane activation.
- Rename editing.
- Split-divider dragging.
- Scroll accumulation.
- Host input forwarding.

The next useful boundary is not another callback bundle. It is a small ordered routing pipeline where each route returns `Consumed`, `Pass`, or a transformed event.

### 11. `main.cpp` contains too much session-process behavior

`main.cpp` is about 940 lines. Argument parsing has been extracted, but main still owns:

- Console setup.
- Session-owner launch and retry.
- Attach/list/kill/rename modes.
- Provider registration.
- Render-test mode.
- Windowed application startup.

Move session CLI behavior and self-launch mechanics into a `session_cli` or `session_owner_launcher` library. That would also place the Windows and POSIX launch code beside one another and make the macOS fork issue easier to test.

### 12. `IHost` is convenient but becoming a capability grab bag

The flat interface in [host.h](/D:/dev/Draxul/libs/draxul-host/include/draxul/host.h:149) combines:

- Lifecycle.
- Frame production.
- Keyboard, text, IME, mouse, and wheel input.
- Config and font updates.
- Status/debug information.
- Actions and close requests.
- Working directory and exit status.
- ImGui attachment.
- Scroll offset.

Default no-ops lower the cost of adding a host, but they also make unsupported behavior invisible and invite further interface growth.

Do not restore the obsolete inheritance hierarchy. Prefer small optional capability interfaces or metadata:

- `IHostInputSink`
- `IHostStatusProvider`
- `IGridHostSurface`
- `IImGuiHostConsumer`

The base `IHost` can remain lifecycle and draw focused.

### 13. Host registration is not self-describing

`HostProviderRegistry` is a good construction boundary, but it stores only factories. The command palette separately hardcodes host kinds in [command_palette.cpp](/D:/dev/Draxul/app/command_palette.cpp:59), including optional MegaCity, BioView, and SatView.

An optional-off build can therefore advertise compound actions for hosts that have no provider.

Provider metadata should include:

- Kind and display name.
- Platform availability.
- Whether the provider is currently registered.
- Supported launch arguments.
- Whether it should appear in the command palette.
- Optional icon/category.

CLI help and palette choices should be derived from that metadata.

### 14. Product modules are isolated externally but monolithic internally

Largest examples:

- MegaCity Vulkan backend: approximately 4,994 lines.
- MegaCity host: approximately 2,992 lines.
- SatView host: approximately 3,198 lines.
- SatView Vulkan backend: approximately 2,315 lines.
- MegaCity semantic layout: approximately 2,217 lines.
- SatView Metal shader: approximately 1,304 lines.

SatView’s single static library contains catalog parsing, HTTP/cache services, configuration, filtering, geometry, ephemerides, simulation threading, UI, host state, and both renderer backends.

A more agent-friendly split would be:

```text
draxul-satview-core
  catalogs, coordinates, filters, ephemerides, propagation

draxul-satview-services
  cache and network refresh

draxul-satview-scene
  backend-neutral scene records and uniforms

draxul-satview-host
  UI, input, camera, and orchestration

draxul-satview-renderer
  platform backend selected privately
```

MegaCity is farther along with its internal split, but its renderer backends still need decomposition by pass/resource family.

### 15. CPU, GLSL, and Metal contracts depend too heavily on convention

The renderer code manually duplicates:

- Uniform and push-constant layouts.
- Binding indices.
- Material and texture identifiers.
- Projection math.
- Pass ordering and feature flags.

Shared C++ constants and GLSL includes help, but Metal remains largely manually synchronized. There is no automated reflection or ABI validation proving that C++ structures and shaders agree.

This is a high-value area for generated bindings, reflection checks, or at least static offset/size manifests tested on both backends.

## Testing assessment

### What is strong

The test suite is unusually broad:

- 142 `*_tests.cpp` files in the current moving snapshot.
- Approximately 1,469 Catch2 cases/scenarios.
- More than 6,000 assertion sites.
- Deterministic redraw fixtures.
- Fake window, renderer, host, and RPC support.
- VT and msgpack fuzz tests.
- Startup rollback coverage.
- Grid, Unicode, ligature, scrollback, configuration, host, and session tests.
- Cross-platform renderer and process tests.
- Render snapshot infrastructure.
- Strong parser validation in the active SatView catalog work.

The new boundary catalog is directionally good: it uses a versioned binary header, size checks, record limits, finite-vector validation, pinned source provenance, attribution, and a truncated-file test.

### Important holes

1. There are no direct weather-service tests.
2. macOS excludes the entire session-attach test file.
3. Real GPU shutdown testing is opt-in and skipped by default.
4. Several resize-cascade cases are literal `SKIP()` placeholders requiring a live renderer or host.
5. Slow fuzz tests require `DRAXUL_RUN_SLOW_TESTS=1`; manual-only CI makes it easy never to run them.
6. Many font-dependent tests can skip, although bundled fonts reduce the normal risk.
7. No MegaCity or SatView visual scenario is registered in the normal snapshot matrix.
8. Shader ABI and Vulkan/Metal parity are not tested mechanically.
9. The monolithic `draxul-tests` executable links the app and every enabled product module, increasing link time and reducing independent module validation.
10. Tests still include several private implementation headers by relative `src/` paths, despite a completed boundary-cleanup item.

Splitting tests by module would provide faster parallel CTest execution and make optional-module coverage explicit.

## Configuration behavior

`reload_config()` in [app.cpp](/D:/dev/Draxul/app/app.cpp:593) reloads configuration, fonts, scroll settings, chord timing, and host reload state. It does not reconfigure the weather service.

Consequences:

- Changing `weather_location` does not switch locations.
- Clearing it does not stop the existing service.
- Adding it to a running app does not start the service.
- A failed font change partially applies unrelated settings while restoring only the font subset.

Partial application may be intentional, but the semantics should be documented and tested. External services should participate through explicit config-diff handlers.

## Build and dependency hygiene

### Positive observations

- Renderer backend headers remain private to the renderer library.
- Public headers and internal headers are generally separated correctly.
- Platform-specific sources are selected in CMake.
- MegaCity and SatView register through the executable rather than introducing terminal-host dependencies.
- The shared text-atlas extraction moves neutral data into `draxul-types` and rasterization into `draxul-font`, which is the right dependency direction.

### Concerns

- `draxul-app` publicly exposes the entire `app/` directory and publicly links most subsystems.
- Module libraries frequently mark large dependency sets as `PUBLIC`.
- `draxul-app-support` is now only an interface compatibility target, yet agent documentation still describes it as the home of config, grid rendering, and render tests.
- SatView repeats almost its full source list in Apple and non-Apple branches.
- Metal shader target wiring is manually repeated while Windows uses a broad glob.
- The format workflow ignores modules.
- The active atlas work currently accepts unconstrained target pixel dimensions. In [text_atlas_builder.cpp](/D:/dev/Draxul/libs/draxul-font/src/text_atlas_builder.cpp:109), width and height are multiplied as `int` before conversion to `size_t`, with no maximum request validation. The active plan already calls for over-limit rejection, but the current three tests cover only determinism, UTF-8, and duplicate/empty keys.

Because that atlas work is actively changing, the last point is a status note rather than a new work-item recommendation.

## Documentation and multi-agent collaboration

### Architecture documentation is stale

The supplied [AGENTS.md](/D:/dev/Draxul/AGENTS.md:68) and [module-map.md](/D:/dev/Draxul/docs/module-map.md:71) still describe:

- `IBaseRenderer -> I3DRenderer -> IGridRenderer`
- `IHost -> I3DHost -> IGridHost`
- Renderer registration through `I3DRenderer`
- `HostManager` downcasting `I3DHost`

Those types and mechanisms no longer exist.

`plans/design/renderers.md` is internally inconsistent: its introduction correctly says the middle tier was removed, while later sections still praise and criticize that old interface.

This is one of the highest-leverage fixes for multiple agents. Incorrect architectural guidance causes plausible but wrong implementations.

### Planning paths have migrated without updating automation

The current canonical planning tree is:

- `kanban/done`: 450 files
- `kanban/ice-box`: 76 files
- `kanban/pending`: empty

There are no live `plans/work-items`, `plans/work-items-complete`, or `plans/work-items-icebox` directories. Nevertheless:

- [plans/README.md](/D:/dev/Draxul/plans/README.md:7) still documents those paths.
- [sync_project_board.py](/D:/dev/Draxul/scripts/sync_project_board.py:29) still reads them and will fail.
- The board query retrieves only `items(first: 100)`, while the local done/icebox corpus alone exceeds 500 cards.

The `done` directory also contains 147 files with roughly 950 unchecked boxes. These were not interpreted as open tasks in this review, but they make it difficult for agents to distinguish:

- A historical implementation plan whose checkboxes were never updated.
- A partially completed item.
- A genuinely completed outcome.

A status header or generated index would remove that ambiguity without rewriting old records.

### Review automation does not enforce its read-only promise

`do_review.py` defaults to parallel unattended execution and enables the Codex-specific safe mode. However, [ask_agent.py](/D:/dev/Draxul/scripts/ask_agent.py:143) gives the Claude helper `Bash,Read,Write,Edit,Glob,Grep` by default, and unattended mode adds full-auto permission bypass.

`Run-Review.ps1` similarly invokes Codex with `danger-full-access`, Gemini with skipped permissions, and Claude with bypass permissions. Prompt text says “review only,” but enforcement is left to model compliance.

For a multi-agent shared checkout, review scripts should enforce:

- Read-only filesystem access.
- No build/test execution when requested.
- No write/edit tools.
- No full-auto permission bypass.
- Isolated output written by the parent process after capturing the model response.

The iceboxed agent-script deduplication item was not duplicated here; this is a separate safety-contract defect.

## Reconciliation with existing work

The recommendations below deliberately exclude current done or iceboxed features such as searchable scrollback, configuration GUI, command-palette MRU, right-click menus, clipboard history, live config reload infrastructure, keybinding inspector, performance HUD, remote Neovim attach, IME composition UI, pane drag reorder, window-state persistence, and the numerous iceboxed stress tests.

They also exclude the active observatory silhouette, cardinal labels, constellation boundaries, constellation names, and shared text-atlas work.

Several findings above overlap historical completed items only because the current tree has regressed from their stated acceptance criteria:

- Push/PR CI is absent.
- Ligature and wide-character render scenarios are not currently runnable.
- Render-test code remains coupled to production.
- Tests again include private source headers.
- Planning-path automation is stale again.
- New post-fork allocations appeared in app-level launch paths.

## Recommended implementation order

1. **Security and lifecycle pass**
   - Replace shell-based weather HTTP.
   - Add cancellable shared network transport.
   - Fix post-fork launch paths.
   - Make session writes atomic.

2. **Make validation truthful**
   - Enable required PR CI.
   - Repair render scenario registration and references.
   - Restore Apple session-attach coverage.
   - Extend formatting to modules.

3. **Repair contracts**
   - Update AGENTS/module-map/renderer design documents.
   - Derive palette host choices from provider metadata.
   - Fix canonical kanban paths and project-board pagination.
   - Enforce read-only agent-review permissions.

4. **Reduce merge hotspots**
   - Extract session coordination and workspace control from `App`.
   - Split Chrome layout from drawing and editing.
   - Split SatView core/services/scene/host/renderer.
   - Divide renderer backends by pass and resource family.

---

# Top 10 good things

1. **Strong cross-platform intent.** Windows/Vulkan and macOS/Metal are first-class paths rather than one being an afterthought.
2. **Good low-level library decomposition.** Types, config, windowing, font, grid, Neovim, renderer, and host responsibilities are visibly separated.
3. **A clean current frame/render-pass model.** `IFrameContext` allows grid, NanoVG, Markdown, MegaCity, and SatView passes to compose without old renderer downcasts.
4. **Exceptional test breadth.** Parsers, lifecycle, input, sessions, Unicode, configuration, RPC, VT behavior, rendering, and failure paths all receive meaningful attention.
5. **Useful deterministic fixtures.** Replay fixtures and shared fakes make difficult UI/RPC behavior reproducible without always launching Neovim or a GPU.
6. **Optional product registration is directionally correct.** MegaCity and SatView register from the executable and do not contaminate the terminal host library with direct source dependencies.
7. **Configuration has matured well.** Parsing, preservation, unknown-key handling, overrides, and typed config structures are much cleaner than a typical GUI project.
8. **Data provenance is taken seriously.** SatView assets have pinned inputs, explicit attribution, versioned formats, and deterministic generators.
9. **The active shared text-atlas extraction is the right abstraction.** Neutral image/entry data belongs in types, while font-dependent rasterization belongs in the font library.
10. **The project records design intent.** Although some records are stale, the volume of plans, outcomes, and feature documentation gives future agents substantially more context than code alone.

# Top 10 bad things

1. **Local weather configuration can become a shell command.**
2. **New macOS session launch paths allocate after `fork()` in a multithreaded application.**
3. **No build, test, format, or sanitizer workflow runs automatically on pushes or pull requests.**
4. **`App`, Chrome, session attach, and the major product renderers are large merge-conflict hotspots.**
5. **Agent-facing architecture documents describe interfaces that no longer exist.**
6. **Render-test coverage is silently skipped while documentation and completed items claim it exists.**
7. **Session persistence truncates final files directly and the Apple attach suite is completely excluded.**
8. **Vulkan and Metal shader/resource contracts are duplicated without mechanical ABI validation.**
9. **Optional module switches do not fully remove Windows shader dependencies.**
10. **Planning and review automation is internally inconsistent, stale, and sometimes write-capable during unattended reviews.**

# Best 10 quality-of-life features to add

These do not duplicate the current done, icebox, or active SatView work.

1. **Crash-recovery session journal**  
   Persist topology changes incrementally and recover the last consistent shell workspace after a crash, rather than only after a clean shutdown.

2. **Safe-mode startup**  
   Add `--safe-mode` and a recovery prompt that bypass user config, restores default fonts, disables network services, and opens diagnostics when normal startup fails.

3. **Global session/workspace/pane switcher**  
   Search across running and saved sessions, workspaces, panes, working directories, and titles from one overlay. This is broader than command-palette MRU ordering.

4. **Detach a pane or workspace into a new OS window**  
   Allow a pane to become a separate window and later rejoin its original workspace.

5. **Busy-process close guard**  
   Detect foreground shell jobs and confirm before closing a pane, workspace, or session that still owns an active process.

6. **First-run health center**  
   Check Neovim, Vulkan/Metal readiness, fonts, shell availability, `curl` or future HTTP transport, config paths, and render assets with actionable fixes.

7. **Accessibility mode**  
   Provide independent chrome/UI scaling, reduced motion, high-contrast focus indicators, color-blind-safe palettes, and accessible labels for native controls.

8. **Terminal graphics protocols**  
   Add Kitty graphics, Sixel, or iTerm2 inline-image support, beginning with one bounded, well-tested protocol.

9. **Portable profile bundle**  
   Export/import configuration, keybindings, theme, shell defaults, and selected session layouts as a versioned archive with secrets and machine-specific paths excluded.

10. **Network and privacy controls**  
    Give weather and SatView services an offline switch, cache inspection, refresh policy, proxy configuration, source disclosure, and “clear downloaded data” action.

# Best 10 tests to improve stability

1. **Weather hostile-input matrix**  
   Exercise quotes, shell metacharacters, Unicode, malformed locations, minified JSON, missing fields, escaped strings, and non-numeric values.

2. **Network-service shutdown deadline**  
   Use a blocked injected transport and prove Weather, SatView catalog, and SatView cloud services stop within a fixed bound.

3. **Atomic session-write fault injection**  
   Interrupt writes at each stage and prove the previous valid state survives or a recoverable backup is selected.

4. **Restore Apple session-attach coverage**  
   Remove the whole-file Catch2 exclusion and run protocol, detach, rename, save-as, kill, and reattach cases on macOS.

5. **Multithreaded self-launch regression test**  
   Launch session-owner and session-picker children while allocator and worker threads are active; preferably test a `posix_spawn` abstraction directly.

6. **Provider-registry availability matrix**  
   Verify CLI help, command-palette actions, parsing, and factory availability with MegaCity and SatView independently enabled and disabled.

7. **Render-manifest integrity test**  
   Require every declared scenario to have its TOML and platform references, and report every unregistered scenario/reference pair.

8. **Shader ABI contract test**  
   Validate C++ uniform sizes, offsets, binding indices, texture counts, and generated/reflected Vulkan and Metal resource layouts.

9. **Overlay initialization failure test**  
   Force grid-handle allocation failures for command palette, toast, chrome text, and diagnostics overlays and verify explicit rollback or documented degradation.

10. **Weather config-reload integration test**  
    Prove adding, changing, and clearing `weather_location` starts, replaces, and stops the service without leaving an old worker running.

# Worst 10 existing features by engineering risk

“Worst” here means highest implementation risk or maintenance cost, not lowest user value.

1. **Weather status pill**  
   Small user benefit, but currently carries shell injection, handwritten JSON parsing, duplicated HTTP plumbing, and blocking shutdown risk.

2. **Persistent session state**  
   Valuable feature, but final-file truncation makes it vulnerable to crash-time data loss.

3. **Live session attach/reattach**  
   Cross-platform security, IPC, process ownership, UI activation, persistence, and command protocol are concentrated in one large subsystem with missing Apple tests.

4. **macOS session picker and owner self-launch**  
   The user workflow is useful, but the current post-fork allocations can produce rare, extremely difficult-to-debug hangs.

5. **Command-palette host expansion**  
   It hardcodes product availability separately from the provider registry and can advertise impossible actions.

6. **Production render-test and NanoVG demo integration**  
   Test-only behavior remains coupled to the shipped app and its registration does not match documentation or the actual snapshot suite.

7. **Chrome tab/status/weather layer**  
   A single draw path mixes vector graphics, grid text, editing state, layout, DPI conversion, and hit-testing.

8. **SatView network ingestion**  
   Catalog and cloud refresh are useful, but add duplicate shell-based transport, cache, timeout, and shutdown concerns to an already large host.

9. **SatView host and renderer**  
   Excellent functionality, but UI, simulation, catalog state, scene construction, and backend concerns are concentrated into several very large files.

10. **MegaCity renderer and host**  
    Strong visual capability, but the backend size and manually duplicated shader/resource contracts make parallel feature work and cross-platform parity increasingly expensive.

