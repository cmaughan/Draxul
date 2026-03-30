# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### Windows
Requires CMake 3.25+, Visual Studio 2022, and Vulkan SDK (with glslc).

```bash
cmake --preset default                              # Configure (Debug, VS 2022 x64)
cmake --preset release                               # Configure (Release)
cmake --build build --config Release --target draxul # Build
```

Run: `.\build\Release\draxul.exe` (requires `nvim` on PATH). Pass `--console` to allocate a debug console window.

### macOS
Requires CMake 3.25+, Xcode Command Line Tools (for Metal compiler).

```bash
cmake --preset mac-debug                             # Configure (Debug)
cmake --preset mac-release                           # Configure (Release)
cmake --preset mac-asan                              # Configure (Debug + AddressSanitizer/LSan)
cmake --build build --target draxul                 # Build
cmake --build build --target draxul-tests           # Build unit tests (with ASan preset: sanitizers enabled)
```

Run: `./build/draxul.app/Contents/MacOS/draxul` or `open ./build/draxul.app` (requires `nvim` on PATH).

To run the unit test suite under ASan: `cmake --preset mac-asan && cmake --build build --target draxul-tests && ctest --test-dir build -R draxul-tests`.

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

```
draxul/
├── CMakeLists.txt                  # Top-level: wires libraries + app
├── CLAUDE.md
├── libs/
│   ├── draxul-types/              # Shared POD types (header-only)
│   │   ├── include/draxul/
│   │   │   ├── types.h             # Color, AtlasRegion, CursorShape, CellUpdate
│   │   │   └── events.h            # WindowResizeEvent, KeyEvent, etc.
│   │   └── CMakeLists.txt
│   ├── draxul-window/             # Window abstraction + SDL implementation
│   │   ├── include/draxul/
│   │   │   ├── window.h            # IWindow interface
│   │   │   └── sdl_window.h        # SdlWindow implementation
│   │   ├── src/sdl_window.cpp
│   │   └── CMakeLists.txt
│   ├── draxul-renderer/           # Renderer interface + backends
│   │   ├── include/draxul/
│   │   │   ├── base_renderer.h     # IBaseRenderer, I3DRenderer, IRenderPass, IRenderContext
│   │   │   └── renderer.h          # IGridRenderer (extends I3DRenderer), RendererBundle
│   │   ├── src/vulkan/             # Vulkan backend (Windows)
│   │   ├── src/metal/              # Metal backend (macOS)
│   │   └── CMakeLists.txt
│   ├── draxul-font/               # Font loading, shaping, glyph cache
│   │   ├── include/draxul/
│   │   │   ├── font_metrics.h      # FontMetrics struct
│   │   │   └── text_service.h      # TextService glyph atlas + metrics API
│   │   ├── src/
│   │   └── CMakeLists.txt
│   ├── draxul-app-support/        # Reusable app-layer helpers shared by the app and tests
│   │   └── CMakeLists.txt
│   ├── draxul-grid/               # 2D cell grid + highlight table
│   │   ├── include/draxul/
│   │   │   └── grid.h              # Grid, Cell, HlAttr, HighlightTable
│   │   ├── src/grid.cpp
│   │   └── CMakeLists.txt
│   └── draxul-nvim/               # Neovim process, RPC, UI events, input
│       ├── include/draxul/
│       │   └── nvim.h              # NvimProcess, NvimRpc, UiEventHandler, NvimInput
│       ├── src/
│       └── CMakeLists.txt
├── app/                            # The executable — just wiring
│   ├── app.h/cpp
│   ├── main.cpp
│   └── renderer_factory.cpp
├── shaders/
└── fonts/
```

## Architecture

Draxul is a Neovim GUI frontend. It spawns `nvim --embed`, communicates via msgpack-RPC over stdin/stdout pipes, and renders the terminal grid using the platform's GPU API (Vulkan on Windows, Metal on macOS).

### Dependency Graph (libraries only link downward)

```
                    draxul-types (header-only)
                   /      |       |       \
          window   renderer   font    grid
            |         |        |       |
            └────┬────┘        └───┬───┘
                 |                 |
              draxul-nvim --------┘
                 |
        draxul-app-support
                 |
                app (executable)
```

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

- **Renderer hierarchy** (`libs/draxul-renderer/include/draxul/`): `IBaseRenderer` → `I3DRenderer` → `IGridRenderer`. The grid renderer IS-A 3D renderer IS-A base renderer. `MetalRenderer` and `VkRenderer` implement `IGridRenderer` and transitively satisfy both upper tiers.
  - `IRenderPass` / `IRenderContext` (`base_renderer.h`): typed render pass abstraction replacing the legacy `void*` callback. Any subsystem can register a pass with `I3DRenderer::register_render_pass(shared_ptr<IRenderPass>)`; the renderer calls `IRenderPass::record(IRenderContext&)` each frame.
- **Host hierarchy** (`libs/draxul-host/include/draxul/host.h`): `IHost` → `I3DHost` → `IGridHost` → `GridHostBase`. Terminal/Neovim hosts inherit `GridHostBase` (which provides no-op 3D hooks). `MegaCityHost` inherits `I3DHost` directly and registers a `CubeRenderPass` via `attach_3d_renderer()`.
  - `HostManager` calls `attach_3d_renderer()` post-`initialize()` for any host that is `I3DHost` (one-shot `dynamic_cast` at startup only).
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
- GUI-only shortcuts are configured under a `[keybindings]` table with action names such as `toggle_diagnostics`, `copy`, `paste`, `font_increase`, `font_decrease`, and `font_reset`.
- Keep GUI keybinding changes in the Draxul layer only; Neovim key remapping still belongs in Neovim config.

## Validation Expectations

- **Always build and run the smoke test before committing.** Use `cmake --build build --target draxul draxul-tests` followed by `py do.py smoke` (or `python do.py smoke` on Windows). This catches broken includes, link errors, and basic startup failures that only surface after merging changes from multiple sources.
- If you touch RPC, redraw handling, or input translation, run `ctest`.
- If you touch renderer code, build the platform-specific app target and verify startup at least once.
- After implementing a user-facing feature or rendering-affecting change, run the render smoke/snapshot suite with `t.bat` or `ctest` and confirm the relevant `draxul-render-*` scenario still passes.
- When blessing render references, use `py do.py blessbasic`, `py do.py blesscmdline`, `py do.py blessunicode`, `py do.py blessligatures`, or `py do.py blessall` from the repo root instead of calling `draxul.exe --render-test` manually.
- If you change build wiring, keep both Windows and macOS paths valid in CI.
- Do not run `clang-format` manually in this repo. The pre-commit hook runs `clang-format` automatically on staged files, so if formatting is needed the first commit attempt may fail; re-stage the hook's edits and retry the commit.
- When you complete a work item or a concrete subtask from `plans/work-items/*.md`, update that markdown file in the same turn and mark the completed entries with Markdown task ticks (`- [x]`). Leave incomplete follow-ups as unchecked items so progress stays visible in the file itself.
- When a work item from `plans/work-items/*.md` is fully complete, move it to `plans/work-items-complete/` in the same turn and update any index/reference links that still point at the old location.
- After implementing a new user-facing feature, configuration option, CLI flag, or build/CI change, update `docs/features.md` to include it. This file is the canonical reference for what the app already supports — keeping it current prevents future agents from proposing work items for features that already exist.
- Before creating new work items, check `docs/features.md` to verify the proposed feature or capability is not already implemented.

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
