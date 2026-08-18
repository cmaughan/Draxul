# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Canonical agent guide

CLAUDE.md is the single source of truth for shared rules — build commands, architecture,
threading invariants, the `kanban/` tracker, validation expectations, and known pitfalls —
for every agent family. `AGENTS.md` and `GEMINI.md` are thin pointers with only genuinely
model-specific notes; `learnings_agents.md` is a retrospective, not a rule source. Change
a shared rule here and nowhere else.

The detailed current library and product-module graph lives in
[docs/module-map.md](docs/module-map.md). CMake is authoritative when the prose and build
files disagree.

## Scope

Draxul is a cross-platform Neovim GUI frontend that also supports Bash, Zsh,
PowerShell, and WSL shell hosts. It uses SDL3 for windowing/input, Vulkan on Windows,
Metal on macOS, and msgpack-RPC over pipes to communicate with `nvim --embed`.

Unless a task is explicitly scoped to one platform or backend, user-facing features
and fixes must keep both Windows and macOS working. When touching platform, renderer,
process, input, or shell-host behavior, inspect the corresponding paths for both
platforms and call out any unsupported gap explicitly.

Keep `app/` focused on orchestration. Put reusable platform or subsystem behavior in
`libs/`, and product-specific behavior in its owning directory under `modules/`.

## Build Commands

### Windows
Requires CMake 3.25+, Visual Studio 2022, and Vulkan SDK (with glslc).

```bash
py do.py build debug    # Default development build: Ninja Debug
py do.py run debug      # Incremental Debug build and launch
py do.py test debug     # Same Ninja Debug cache; parallel core unit tests
py do.py run release    # Final Release build and startup confirmation
```

`--vs` remains available for a Visual Studio generator check when a change is
specifically VS/build-system-facing. Pass `--console` to allocate a debug console
window. Raw CMake presets remain supported, but are not the default agent workflow.

### macOS
Requires CMake 3.25+, Xcode Command Line Tools (for Metal compiler).

```bash
python3 do.py build debug                            # Configure/build Debug
python3 do.py run debug                              # Incremental Debug build and launch
python3 do.py test debug                             # Same Debug cache; parallel core unit tests
python3 do.py run release                            # Final Release build and startup confirmation
cmake --preset mac-asan                              # Configure (Debug + AddressSanitizer/LSan)
cmake --preset mac-tsan                              # Configure (Debug + ThreadSanitizer)
```

Run: `./build/draxul.app/Contents/MacOS/draxul` or `open ./build/draxul.app` (requires `nvim` on PATH).

To run the unit test suite under ASan: `cmake --preset mac-asan && cmake --build build --target draxul-tests --parallel && ctest --test-dir build -R draxul-tests --parallel 4`.

To run the unit test suite under TSan (ThreadSanitizer — mutually exclusive with ASan, so it uses its own preset): `cmake --preset mac-tsan && cmake --build build --target draxul-tests --parallel && ctest --test-dir build -R draxul-tests --parallel 4`.

TSan suppressions for third-party library noise (SDL3, Metal, system frameworks) live in `tsan.supp` at the repo root. When running TSan locally, set `TSAN_OPTIONS="suppressions=tsan.supp"`.

### Convenience scripts

- `do build`, `do run`, and `do test` share the same configuration and generator
  selection. They default to Debug and Ninja on Windows; use `release` only for
  the final confirmation or when optimized behavior is relevant.
- `do smoke --skip-build` runs the startup check against the already-built selected
  cache. Omit `--skip-build` when no preceding build/test has produced the app.
- `t.bat` / `t.sh` remain explicit broad validation wrappers, not the normal
  edit-build-test path.

### Debugging / Logging

Use the `--log-file` and `--log-level` CLI flags for debug logging. These are reliable on all platforms (env vars like `DRAXUL_LOG_FILE` do not propagate into macOS `.app` bundles).

```bash
./build/draxul.app/Contents/MacOS/draxul --host zsh --log-file /tmp/debug.log --log-level debug
```

- `--log-level <level>` — set minimum log level: `error`, `warn`, `info`, `debug`, `trace`. Defaults to `debug` when `--log-file` is given without `--log-level`. Logs always go to stderr (the launching terminal), so `--log-level debug` alone is enough for console debugging.
- `--log-file <path>` — additionally write logs to a file (useful when stderr is not visible).
- Use `DRAXUL_LOG_DEBUG(LogCategory::App, "fmt", ...)` for temporary instrumentation; these compile to real calls (not stripped) so they appear whenever the level is set to `debug` or lower.
- Env vars (`DRAXUL_LOG`, `DRAXUL_LOG_FILE`, `DRAXUL_LOG_CATEGORIES`) still work when launching the binary directly (not via `open`), but prefer the CLI flags.

## Project Structure

- `app/`: executable and `draxul-app` orchestration target.
- `libs/`: reusable infrastructure libraries; see the complete ownership list in
  [docs/module-map.md](docs/module-map.md#core-libraries).
- `modules/markdown/` and `modules/kanban/`: product modules built by default.
- `plugins/megacity/`, `plugins/satview/`, `plugins/scoreview/`: **git
  submodules** for the product plugin repositories
  ([draxul-megacity](https://github.com/cmaughan/draxul-megacity),
  [draxul-satview](https://github.com/cmaughan/draxul-satview),
  [draxul-scoreview](https://github.com/cmaughan/draxul-scoreview)), gated by
  `DRAXUL_ENABLE_MEGACITY` / `DRAXUL_ENABLE_SATVIEW` /
  `DRAXUL_ENABLE_SCOREVIEW`. Each product owns its sources, dependencies,
  shaders, assets, tests, docs, plans, and kanban cards in its own repo.
- `plugins/spinning-triangle/`: the in-repo reference plugin and ABI test
  vehicle; `plugins/support/`: generic plugin-support code. Both stay in this
  repository.
- `sdk/`: the public C plugin ABI (`Draxul::PluginSDK`).
- `tests/`: unit, integration, performance, and render-snapshot coverage.
- `docs/`: canonical feature inventory, module map, generated diagrams, and API docs.
- `kanban/`: the only work-item tracker for core; `plans/` contains designs and
  research. Product-specific cards and plans live in the product repos.

When working under `plugins/megacity/`, also read `plugins/megacity/product/AGENTS.md`.

### Submodule workflow

- Fresh clone: `git clone --recurse-submodules`; after pulling:
  `git submodule update --init`. An uninitialized product submodule is a
  supported state — configure skips it with a STATUS message (CI hard-fails
  instead via `DRAXUL_REQUIRE_ENABLED_PLUGINS`).
- A change inside `plugins/megacity|satview|scoreview` is a commit in that
  product's repository, pushed there, then adopted here with a deliberate
  submodule pointer-bump commit. Never commit a pointer bump as a drive-by in
  an unrelated change; `git submodule update` snaps an unwanted local pointer
  move back.
- Core-seam changes (SDK, `Draxul::PluginSupport::*` allowlist) land in this
  repo first; products update against them afterwards.

## Architecture

Draxul is a Neovim GUI frontend. It spawns `nvim --embed`, communicates via msgpack-RPC over stdin/stdout pipes, and renders the terminal grid using the platform's GPU API (Vulkan on Windows, Metal on macOS).

### Dependency graph

The graph is no longer a single linear stack: core infrastructure fans out from
the narrow `draxul-types`, `draxul-performance`, `draxul-bmp`, and
`draxul-host-identity` foundations; runtime and host composition sit above them, and product
modules connect to the executable through host targets. See
[docs/module-map.md](docs/module-map.md#dependency-shape) for the maintained high-level
graph and each `CMakeLists.txt` for exact target edges.

### Data flow

```
nvim --embed (child process)
  → [msgpack-RPC over pipes, reader thread]
  → NvimRpc notification queue
  → App::run() drains queue each frame
  → UiEventHandler parses ext_linegrid "redraw" events → Grid (2D cell array with dirty tracking)
  → App::update_grid_to_renderer() resolves highlights, shapes text, rasterizes glyphs
  → Renderer buffer write (GpuCell array, 112 bytes/cell)
  → Two-pass instanced draw: background quads, then alpha-blended foreground glyphs
```

### Key abstractions

- **Renderer hierarchy**: `IBaseRenderer` lives in `libs/draxul-plugin-support/include/draxul/base_renderer.h` (shared with plugins); `IGridRenderer` extends it in `libs/draxul-renderer/include/draxul/renderer.h`. `MetalRenderer` and `VkRenderer` implement `IGridRenderer`.
  - `IRenderPass` / `IRenderContext` (`base_renderer.h`): typed render pass abstraction. A pass is recorded via `IBaseRenderer::record_render_pass(IRenderPass&, viewport)`; the renderer hands each pass an `IRenderContext` with the per-frame platform handles.
- **Host hierarchy** (`libs/draxul-host/include/draxul/`): `IHost` (`host.h`) is the base; `GridHostBase` / `TerminalSurfaceHostBase` / `TerminalHostBase` provide shared grid, terminal-surface (selection/copy-mode/mouse), and terminal behavior for the shell, remote-terminal, and Neovim hosts; `PluginHost` (`plugin_host.h`) hosts dynamically loaded product plugins across the versioned C ABI. Products (SatView, MegaCity, ScoreView) have no core host classes.
- **IWindow** (`libs/draxul-window/include/draxul/window.h`) — abstract window interface. The renderer knows nothing about fonts, neovim, or text — only colored rectangles and textured quads at grid positions.
- **App** (`app/app.h/cpp`) is the orchestrator that owns all subsystems and runs the main loop.
- Platform-specific renderer implementations live in `libs/draxul-renderer/src/vulkan/` (Windows) and `libs/draxul-renderer/src/metal/` (macOS).

### Rendering

- Buffer indexed by instance index — no vertex buffers, quads generated procedurally in vertex shaders
- Two passes per frame: BG (opaque colored quads) then FG (alpha-blended glyph quads from atlas)
- Host-visible/shared buffer — direct writes, no staging (grid is small)
- Glyph atlas: 2048x2048 RGBA8 texture (4 bytes/pixel), shelf-packed, incremental upload
- 2 frames in flight with synchronization primitives
- Pixel format: BGRA8 Unorm (not SRGB — neovim sends colors already in sRGB)

#### Vulkan-specific (Windows)
- SSBO with host-visible coherent memory via VMA
- Descriptor sets, render passes, swapchain management via vk-bootstrap
- Shaders: GLSL 4.50 compiled to SPIR-V via glslc

#### Metal-specific (macOS)
- MTLBuffer with shared storage mode (CPU+GPU visible)
- CAMetalLayer for drawable management
- Shaders: Metal Shading Language compiled to .metallib via xcrun

### Threading

- **Main thread**: SDL events, nvim message processing, grid mutation, GPU rendering
- **Reader thread**: blocking reads from nvim stdout, MPack decode, push to thread-safe queue

All grid and GPU state is only touched by the main thread.

### Neovim RPC

- MPack library with `MPACK_EXTENSIONS=1` (required for neovim's ext types: Buffer/Window/Tabpage)
- Handles `grid_line` run-length encoding, double-width chars, multi-byte UTF-8
- Only renders on `flush` events

### Font pipeline

- FreeType loads face → HarfBuzz shapes codepoints to glyph IDs → GlyphCache rasterizes on-demand with shelf-packing → atlas uploaded to renderer

## Dependencies

All fetched automatically via CMake FetchContent (in `cmake/FetchDependencies.cmake`): SDL3, FreeType, HarfBuzz, MPack. On Windows: vk-bootstrap, VMA. Shaders compiled from GLSL to SPIR-V via glslc (Windows, `cmake/CompileShaders.cmake`) or from Metal to metallib via xcrun (macOS, `cmake/CompileShaders_Metal.cmake`).

- **GLM** is the preferred library for vector and matrix types (`glm::vec2`, `glm::vec3`, `glm::vec4`, `glm::mat4`, etc.). Use GLM rather than custom structs or other math libraries.

## Config Notes

- User settings live in `config.toml`.
- `enable_ligatures = true/false` controls whether Draxul combines eligible two-cell programming ligatures during shaping; it defaults to `true`.
- `smooth_scroll = true/false` enables trackpad momentum-style scroll accumulation; defaults to `true`.
- `scroll_speed = 1.0` is a multiplier applied to the raw scroll delta before accumulation in the smooth-scroll path. Range: (0.1, 10.0]; values outside this range log a WARN and fall back to `1.0`. Values below `1.0` slow scrolling; values above `1.0` speed it up.
- `enable_toast_notifications = true/false` toggles the corner toast overlay; defaults to `true`. When `false`, all `push_toast` calls are dropped (warnings still log).
- `toast_duration_s = 4.0` controls how long each toast stays visible before fading. Range: 0.5--60.0; out-of-range values are clamped.
- GUI-only shortcuts are configured under a `[keybindings]` table with action names such as `toggle_diagnostics`, `copy`, `paste`, `font_increase`, `font_decrease`, and `font_reset`.
- Keep GUI keybinding changes in the Draxul layer only; Neovim key remapping still belongs in Neovim config.

## Validation Expectations

- Prefer integration tests, vertical-slice tests, and smoke tests that exercise
  real feature boundaries over narrow unit tests with little incremental value.
  Reserve new unit tests for cases where isolation materially improves coverage,
  protects an existing behavior, or makes an otherwise difficult edge case
  deterministic. Do not add small-scale tests merely because a helper or parser
  can be tested in isolation.
- Use one build tree throughout a task. The normal edit-build-test loop is
  `py do.py build debug`, `py do.py run debug`, and `py do.py test debug`; all
  use the Ninja Debug cache on Windows. Do not alternate between Visual Studio,
  Ninja, Debug, and Release trees unless the change specifically requires that
  matrix.
- Keep build and test execution parallel whenever the tool supports it. The
  `do.py` paths supply bounded parallelism for both compilation and CTest.
- `do.py test` is core-scoped by default. Add `--megacity`, `--satview`, or
  `--scoreview` only when that product or a seam it consumes changed. Use
  `--products` when shared plugin SDK/support/renderer changes can affect every
  product, and `--all` only for an explicitly requested complete unit inventory.
- During implementation, build the narrowest affected target and run focused
  tests. Do not repeatedly stack overlapping aggregate builds, `do.py test`,
  broad CTest, smoke, and render suites after each small edit.
- **Before committing, build the core test aggregate once and run smoke from that same
  cache:** `py do.py test debug` followed by `py do.py smoke --skip-build` (or
  `python do.py ...`). This catches broken includes, link errors, unit failures,
  and basic startup failures without rebuilding through another generator. Add
  the relevant product scope to the test command when product code changed.
- On Windows, run process-launch tests before starting a long-lived server from
  that build tree where practical. A server intentionally keeps
  `draxul-server.exe` open; after `draxul.exe` is relinked, helper-refresh tests
  cannot replace that same-cache helper until the exact server is safely stopped.
  Inspect connected clients and live terminals before stopping it; do not evade
  the lock by silently switching the whole validation pass to another generator.
- Finish a completed feature or bug fix with `py do.py run release` and confirm
  startup. In a headless/non-interactive environment, use
  `py do.py run release --console -- --smoke-test` so the Release run exits
  deterministically.
- If you touch RPC, redraw handling, or input translation, run the relevant CTest
  selection from the active `do.py` build tree with `--parallel` (normally through
  `do.py test debug`), broadening to the full suite only when the change warrants it.
- If you touch renderer code, build the platform-specific app target and verify startup at least once.
- After implementing a user-facing feature or rendering-affecting change, run the
  relevant render smoke/snapshot scenario and confirm its `draxul-render-*` test
  passes. Run the full render inventory only for shared renderer/harness changes.
- When blessing render references, use `py do.py blessbasic`, `py do.py blesscmdline`, `py do.py blessunicode`, `py do.py blessnanovg`, or `py do.py blessall` from the repo root instead of calling `draxul.exe --render-test` manually.
- If you change build wiring, keep both Windows and macOS paths valid in CI.
- When a change touches a platform that is unavailable in the current environment,
  use the local build/tests as the interactive handoff gate. Push or dispatch the
  remote cross-platform CI, leave the platform-specific tracker checkbox pending,
  and schedule a follow-up (normally about 30 minutes later) to inspect the result.
  Do not keep the user-facing turn open solely waiting for remote CI unless the user
  explicitly asked you to monitor it synchronously.
- After each completed implementation slice, give the user a brief validation cost
  summary. Break out configure/generate, compilation, focused tests, aggregate tests,
  smoke or render checks, and remote CI as applicable; for each, report the number of
  targets/tests/scenarios, elapsed time, and pass/fail result. Explicitly identify
  skipped, pending, repeated, or overlapping steps so extra validation work remains
  visible instead of being folded into a generic "tests passed" statement.
- Do not run `clang-format` manually in this repo. The pre-commit hook runs `clang-format` automatically on staged files, so if formatting is needed the first commit attempt may fail; re-stage the hook's edits and retry the commit.
- Work items live in `kanban/` — `kanban/pending/` (active), `kanban/ice-box/` (deferred), `kanban/done/` (complete). See [Work Items](#work-items) below.
- When you complete a work item or a concrete subtask from `kanban/pending/*.md`, update that markdown file in the same turn and mark the completed entries with Markdown task ticks (`- [x]`). Leave incomplete follow-ups as unchecked items so progress stays visible in the file itself.
- When a work item is fully complete, move it from `kanban/pending/` to `kanban/done/` in the same turn and update any index/reference links that still point at the old location.
- After implementing a new user-facing feature, configuration option, CLI flag, or build/CI change, update `docs/features.md` to include it. This file is the canonical reference for what the app already supports — keeping it current prevents future agents from proposing work items for features that already exist.
- Before creating new work items, check `docs/features.md` to verify the proposed feature or capability is not already implemented.

## Work Items

Work items live in `kanban/`. This is the only tracker — `plans/` holds design docs and
research notes, never work items.

| Directory | Purpose |
|-----------|---------|
| `kanban/pending/` | Active items — in scope for the current or next work session |
| `kanban/ice-box/` | Deferred items — good ideas, not yet scheduled |
| `kanban/done/` | Completed items — kept for reference |

File naming: `<number> <slug> -<type>.md`

- **number** — priority/sequence. Unique within `kanban/pending/`, but numbers are reused
  across waves and can collide between directories.
- **slug** — hyphenated short description.
- **type** — one of `bug`, `test`, `feature`, `refactor`.

**Always cross-reference an item by its full filename**, never by number alone, because
numbers are reused:

```
# Good
See kanban/ice-box/20 url-detection-click -feature.md

# Bad — ambiguous
See item 20
```

`python do.py syncboard` syncs `kanban/pending` (as Backlog) and `kanban/ice-box` (as
IceBox) to the GitHub project board; it is idempotent.

## Platform

- **Windows**: MSVC/Visual Studio 2022. Process spawning uses `CreateProcess` with piped stdin/stdout. Built as a Windows GUI app (`WIN32_EXECUTABLE`).
- **macOS**: Clang/Xcode. Process spawning uses `fork()`/`exec()` with `pipe()`. Rendering via Metal.

## Known Pitfalls

- Do not include backend-private renderer headers from `app/`.
- Keep shutdown paths non-blocking; a stuck Neovim child must not hang the UI on exit.
- Font-size changes must relayout existing grid geometry even before Neovim acknowledges a resize.
- Unicode rendering is still cell-oriented. Be careful when changing shaping or grid-line parsing because combining clusters and wide glyphs are easy to regress.
- Never duplicate a header between `src/` and `include/draxul/`. Each header lives in exactly one place: public API headers under `include/draxul/` (included with angle brackets), internal headers under `src/` (included with quotes). Maintaining two copies causes them to diverge silently and is flagged by static analysis (SonarCloud).

## Replay Fixtures

Use `tests/support/replay_fixture.h` for redraw-oriented tests. It provides small builders for:

- msgpack-like arrays and maps
- `grid_line` cell batches
- full `redraw` event vectors

This is the preferred way to reproduce UI parsing bugs without launching Neovim.

## Review Consensus

When the user asks to "come to consensus" on reviews, do not just concatenate or summarize review files.

Treat it as a synthesis task:

- read the review notes from the relevant agent folders under `plans/reviews/`
- identify where the agents agree, where one review adds useful detail, and where there is real disagreement or just a sequencing difference
- reconcile the review notes against the current tree so already-fixed or stale issues are called out instead of repeated blindly
- produce a planning-oriented consensus note with suggested fix order, not just a findings list
- where helpful, explicitly attribute points to the agent models that raised them

The result should read like a conversation and planning review for fixes, with a current recommended path forward.

## Consensus Shortcut

When the user says `come to consensus`, treat that as a direct instruction to execute the saved consensus prompt in `plans/prompts/consensus_review.md`.

## Prompt History

When the user asks to store prompts from the current thread, write them to a dated markdown file under `plans/prompts/history/` in chronological order and mark interrupted or partial prompts inline.
