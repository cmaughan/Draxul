# GEMINI.md - Draxul Project Context

## Project Overview

Draxul is a high-performance, cross-platform Neovim GUI frontend built with C++20. It also supports shell hosts (Bash, Zsh, PowerShell, WSL). It leverages native GPU rendering (Vulkan on Windows, Metal on macOS) for low-latency text and grid updates, with SDL3 for windowing/input.

- **Communication:** msgpack-RPC via `nvim --embed` over stdin/stdout pipes
- **Text Stack:** FreeType (loading), HarfBuzz (shaping), dynamic glyph atlas (shelf-packed, RGBA8)
- **Main Goal:** Provide a visually polished and responsive Neovim experience with robust Unicode/emoji/ligature support and native OS integration.

See `docs/features.md` for a complete list of implemented features, configuration options, CLI flags, and build infrastructure.

## Building and Running

### Prerequisites
- **Windows:** CMake 3.25+, Visual Studio 2022, Vulkan SDK (with `glslc`), `nvim` on PATH.
- **macOS:** CMake 3.25+, Xcode CLT, `nvim` on PATH.

### Build Commands

The project uses CMake Presets for configuration.

- **Windows (Debug):**
  ```powershell
  cmake --preset default
  cmake --build build --config Debug --parallel
  ```
- **Windows (Release):**
  ```powershell
  cmake --preset release
  cmake --build build --config Release --parallel
  ```
- **macOS (Debug):**
  ```bash
  cmake --preset mac-debug
  cmake --build build --parallel
  ```
- **macOS (Release):**
  ```bash
  cmake --preset mac-release
  cmake --build build --parallel
  ```
- **macOS (ASan):**
  ```bash
  cmake --preset mac-asan
  cmake --build build --target draxul-tests
  ctest --test-dir build -R draxul-tests
  ```

### Running the App
- **Windows:** `.\build\Release\draxul.exe` (or `.\build\Debug\draxul.exe`)
- **macOS:** `./build/draxul.app/Contents/MacOS/draxul` or `open ./build/draxul.app`
- **Flags:** `--console` (Windows only, opens log console), `--smoke-test` (brief startup check), `--host <type>` (nvim/bash/zsh/powershell/wsl/megacity), `--log-file <path>`, `--log-level <level>`

### Convenience Scripts
- `do run`: Configure, build, and run the application (supports `debug`/`release`, `--vs`/`--ninja`, `--reconfigure`).
- `t.bat` / `t.sh`: Build and run the test suite.

### Debugging / Logging

Use `--log-file` and `--log-level` CLI flags (reliable on all platforms; env vars don't propagate into macOS `.app` bundles).

```bash
./build/draxul.app/Contents/MacOS/draxul --host zsh --log-file /tmp/debug.log --log-level debug
```

- Log levels: `error`, `warn`, `info`, `debug`, `trace`
- Log categories: `App`, `Rpc`, `Nvim`, `Window`, `Font`, `Renderer`, `Input`, `Test`
- Macros: `DRAXUL_LOG_ERROR`, `DRAXUL_LOG_WARN`, `DRAXUL_LOG_INFO`, `DRAXUL_LOG_DEBUG`, `DRAXUL_LOG_TRACE`

## Testing

- **Framework:** Catch2 with CTest runner
- **Run Tests:** `t.bat` (Windows) or `./t.sh` (macOS), or `ctest --test-dir build --output-on-failure`
- **Smoke Test:** `py do.py smoke` (spawns Neovim, verifies flush, exits within timeout)
- **Render Tests:** TOML scenario files in `tests/render/`, reference BMP images in `tests/render/reference/`
- **Bless References:** `py do.py blessbasic`, `blesscmdline`, `blessunicode`, `blessligatures`, `blessall`
- **Replay Fixtures:** Use `tests/support/replay_fixture.h` for redraw-oriented tests without launching Neovim

## Architecture

### Library Structure

- `libs/draxul-types`: Shared POD types, events, logging (header-only)
- `libs/draxul-window`: `IWindow` abstraction and SDL3 implementation
- `libs/draxul-renderer`: Renderer hierarchy (`IBaseRenderer` -> `I3DRenderer` -> `IGridRenderer`), Vulkan and Metal backends
- `libs/draxul-font`: FreeType/HarfBuzz font loading, shaping, glyph atlas
- `libs/draxul-grid`: Cell-based grid model, dirty tracking, highlight table
- `libs/draxul-nvim`: Neovim process management, msgpack-RPC, redraw parsing, input translation
- `libs/draxul-host`: Host abstraction (`IHost` -> `I3DHost` -> `IGridHost` -> `GridHostBase`), HostManager, terminal emulation (VT parser, scrollback, selection, mouse protocols)
- `libs/draxul-app-support`: Config I/O, grid rendering pipeline, render test infrastructure
- `libs/draxul-ui`: ImGui-based diagnostics panel
- `libs/draxul-megacity`: Optional 3D demo host (spinning cube + code visualization)
- `libs/draxul-treesitter`: Optional TreeSitter integration
- `app/`: Orchestration only

### Key Abstractions

- **Renderer hierarchy**: `IBaseRenderer` -> `I3DRenderer` -> `IGridRenderer`. `MetalRenderer` and `VkRenderer` implement `IGridRenderer`.
- **IRenderPass / IRenderContext**: Typed render pass abstraction. Subsystems register passes with `I3DRenderer::register_render_pass()`.
- **Host hierarchy**: `IHost` -> `I3DHost` -> `IGridHost` -> `GridHostBase`. Terminal/Neovim hosts inherit `GridHostBase`. `MegaCityHost` inherits `I3DHost` directly.
- **HostManager**: Manages host lifecycle, split tree layout, and `dynamic_cast<I3DHost*>` for 3D renderer attachment.

### Dependency Direction

- `app` depends on public headers only
- Renderer backends stay private to `draxul-renderer`
- Pure logic stays testable without launching Neovim or opening a window

### Threading

- **Main thread**: SDL events, nvim message processing, grid mutation, GPU rendering
- **Reader thread**: blocking reads from nvim stdout, MPack decode, push to thread-safe queue

All grid and GPU state is only touched by the main thread.

## Dependencies

All fetched via CMake FetchContent: SDL3, FreeType, HarfBuzz, MPack, ImGui, GLM, Catch2. Windows-only: vk-bootstrap, VMA. Shaders: GLSL -> SPIR-V via glslc (Windows), Metal -> metallib via xcrun (macOS).

- **GLM** is the preferred library for vector and matrix types. Use GLM rather than custom structs.

## Config Notes

- User settings live in `config.toml`.
- `enable_ligatures = true/false` — programming ligature combining (default: `true`).
- `smooth_scroll = true/false` — trackpad momentum accumulation (default: `true`).
- `scroll_speed = 1.0` — multiplier, range (0.1, 10.0]; out-of-range logs WARN and resets to `1.0`.
- GUI shortcuts under `[keybindings]` table: `toggle_diagnostics`, `copy`, `paste`, `font_increase`, `font_decrease`, `font_reset`, `split_vertical`, `split_horizontal`, `open_file_dialog`.
- Keep GUI keybinding changes in the Draxul layer only; Neovim key remapping belongs in Neovim config.

## Validation Expectations

- **Always build and run the smoke test before committing.**
- If you touch RPC, redraw handling, or input translation, run `ctest`.
- If you touch renderer code, build the platform-specific app target and verify startup.
- After implementing a user-facing feature, run the render smoke/snapshot suite.
- When blessing render references, use `py do.py bless*` helpers.
- If you change build wiring, keep both Windows and macOS paths valid in CI.
- After every completed work item, run `clang-format`. The pre-commit hook runs it automatically on staged files.
- When you complete a work item from `plans/work-items/*.md`, tick entries and move to `plans/work-items-complete/`.
- After implementing a new feature, config option, CLI flag, or build/CI change, update `docs/features.md`.
- Before creating new work items, check `docs/features.md` to verify the capability is not already implemented.

## Platform

- **Windows**: MSVC/Visual Studio 2022. Process spawning via `CreateProcess` with piped stdin/stdout. Built as a Windows GUI app (`WIN32_EXECUTABLE`).
- **macOS**: Clang/Xcode. Process spawning via `fork()`/`exec()` with `pipe()`. Rendering via Metal.

## Known Pitfalls

- Do not include backend-private renderer headers from `app/`.
- Keep shutdown paths non-blocking; a stuck Neovim child must not hang the UI on exit.
- Font-size changes must relayout existing grid geometry even before Neovim acknowledges a resize.
- Unicode rendering is still cell-oriented. Be careful when changing shaping or grid-line parsing.
- Never duplicate a header between `src/` and `include/draxul/`. Each header lives in exactly one place.

## Key Files

- `app/main.cpp`: Entry point, CLI arg parsing, platform init
- `app/app.cpp`: Main application orchestrator
- `app/input_dispatcher.h/cpp`: Input routing (GUI actions vs host dispatch)
- `app/split_tree.h/cpp`: Binary split tree for pane layout
- `libs/draxul-renderer/include/draxul/renderer.h`: Public grid rendering interface
- `libs/draxul-renderer/include/draxul/base_renderer.h`: Base renderer + render pass abstractions
- `libs/draxul-host/include/draxul/host.h`: Host interface hierarchy
- `libs/draxul-nvim/src/ui_events.cpp`: Neovim UI redraw event processing
- `libs/draxul-app-support/include/draxul/app_config_types.h`: Config struct definitions
- `CMakeLists.txt`: Root build configuration
- `cmake/FetchDependencies.cmake`: External library management
- `docs/features.md`: Canonical feature reference

## Review Consensus

When the user asks to "come to consensus" on reviews, treat it as a synthesis task:

- read review notes from `plans/reviews/`
- identify agreements, additions, and real disagreements
- reconcile against the current tree (flag stale/fixed issues)
- produce a planning-oriented consensus note with fix order
- attribute points to the agent models that raised them

## Consensus Shortcut

When the user says `come to consensus`, execute the saved consensus prompt in `plans/prompts/consensus_review.md`.

## Prompt History

When the user asks to store prompts from the current thread, write them to a dated markdown file under `plans/prompts/history/` in chronological order.
