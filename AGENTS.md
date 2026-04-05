# Draxul Agent Guide

## Scope

Draxul is a cross-platform Neovim GUI frontend (also supports shell hosts: Bash, Zsh, PowerShell, WSL). It renders the terminal grid using the platform's GPU API (Vulkan on Windows, Metal on macOS), with SDL3 for windowing/input, and communicates with `nvim --embed` via msgpack-RPC over stdin/stdout pipes.

The codebase is intentionally split into small libraries. Keep app code thin and push platform or subsystem logic downward into `libs/`. See `docs/features.md` for a complete list of implemented features.

## Build And Test

### Windows

Requirements: CMake 3.25+, Visual Studio 2022, Vulkan SDK with `glslc`, `nvim` on `PATH`.

```powershell
cmake --preset default                              # Configure (Debug, VS 2022 x64)
cmake --preset release                               # Configure (Release)
cmake --build build --config Release --target draxul # Build
ctest --test-dir build --build-config Release --output-on-failure
```

Run: `.\build\Release\draxul.exe` (pass `--console` for a debug console window).

### macOS

Requirements: CMake 3.25+, Xcode Command Line Tools, `nvim` on `PATH`.

```bash
cmake --preset mac-debug                             # Configure (Debug)
cmake --preset mac-release                           # Configure (Release)
cmake --preset mac-asan                              # Configure (Debug + AddressSanitizer/UBSan)
cmake --build build --target draxul                 # Build
cmake --build build --target draxul-tests           # Build unit tests
```

Run: `./build/draxul.app/Contents/MacOS/draxul` or `open ./build/draxul.app`.

ASan test run: `cmake --preset mac-asan && cmake --build build --target draxul-tests && ctest --test-dir build -R draxul-tests`.

### Convenience Scripts

- `do run`: Configure, build, and run the application (supports `debug`/`release`, `--vs`/`--ninja`, `--reconfigure`).
- `t.bat` / `t.sh`: Build and run the test suite.

### Debugging / Logging

Use the `--log-file` and `--log-level` CLI flags for debug logging.

```bash
./build/draxul.app/Contents/MacOS/draxul --host zsh --log-file /tmp/debug.log --log-level debug
```

- `--log-level <level>` — `error`, `warn`, `info`, `debug`, `trace`. Defaults to `debug` when `--log-file` is given.
- `--log-file <path>` — additionally write logs to a file.
- Use `DRAXUL_LOG_DEBUG(LogCategory::App, "fmt", ...)` for temporary instrumentation.
- Env vars (`DRAXUL_LOG`, `DRAXUL_LOG_FILE`, `DRAXUL_LOG_CATEGORIES`) work when launching the binary directly but not via macOS `open`.

## Architecture

### Library Structure

- `libs/draxul-types`: Shared POD types, events, logging (header-only)
- `libs/draxul-window`: `IWindow` abstraction and SDL3 implementation
- `libs/draxul-renderer`: `IBaseRenderer` / `I3DRenderer` / `IGridRenderer` hierarchy, Vulkan and Metal backends
- `libs/draxul-font`: FreeType/HarfBuzz font loading, shaping, glyph atlas management
- `libs/draxul-grid`: Cell-based grid model, dirty tracking, highlights
- `libs/draxul-nvim`: Neovim process, msgpack-RPC transport, redraw parsing, input translation
- `libs/draxul-host`: Host abstraction (`IHost` / `I3DHost` / `IGridHost` / `GridHostBase`), HostManager, terminal emulation
- `libs/draxul-app-support`: Reusable app-layer helpers (config, grid rendering pipeline, render tests)
- `libs/draxul-ui`: ImGui-based diagnostics panel
- `libs/draxul-megacity`: Optional 3D demo host (spinning cube + code visualization)
- `libs/draxul-treesitter`: Optional TreeSitter integration for code analysis
- `app/`: Orchestration only — owns subsystems and runs main loop

### Dependency Direction

- `app` depends on public headers only
- Renderer backends stay private to `draxul-renderer`
- Pure logic stays testable without launching Neovim or opening a window

### Key Abstractions

- **Renderer hierarchy**: `IBaseRenderer` -> `I3DRenderer` -> `IGridRenderer`. Metal and Vulkan implement `IGridRenderer`.
- **Host hierarchy**: `IHost` -> `I3DHost` -> `IGridHost` -> `GridHostBase`. Terminal/Neovim hosts inherit `GridHostBase`. `MegaCityHost` inherits `I3DHost` directly.
- **IRenderPass / IRenderContext**: Typed render pass abstraction. Subsystems register passes with `I3DRenderer::register_render_pass()`.
- **HostManager**: Manages host lifecycle, split tree, and `dynamic_cast<I3DHost*>` for 3D renderer attachment.

### Threading

- **Main thread**: SDL events, nvim message processing, grid mutation, GPU rendering
- **Reader thread**: blocking reads from nvim stdout, MPack decode, push to thread-safe queue

All grid and GPU state is only touched by the main thread.

## Dependencies

All fetched automatically via CMake FetchContent: SDL3, FreeType, HarfBuzz, MPack, ImGui, GLM, Catch2. Windows-only: vk-bootstrap, VMA. Shaders compiled from GLSL to SPIR-V via glslc (Windows) or Metal to metallib via xcrun (macOS).

- **GLM** is the preferred library for vector and matrix types (`glm::vec2`, `glm::vec3`, `glm::vec4`, `glm::mat4`, etc.). Use GLM rather than custom structs or other math libraries.

## Config Notes

- User settings live in `config.toml`.
- `enable_ligatures = true/false` controls programming ligature combining; defaults to `true`.
- `smooth_scroll = true/false` enables trackpad momentum-style scroll accumulation; defaults to `true`.
- `scroll_speed = 1.0` — multiplier applied to raw scroll delta. Range: (0.1, 10.0]; out-of-range values log a WARN and fall back to `1.0`.
- GUI shortcuts configured under `[keybindings]` table with actions: `toggle_diagnostics`, `copy`, `paste`, `font_increase`, `font_decrease`, `font_reset`, `split_vertical`, `split_horizontal`, `open_file_dialog`.
- Keep GUI keybinding changes in the Draxul layer only; Neovim key remapping belongs in Neovim config.

## Validation Expectations

- **Always build and run the smoke test before committing.** Use `cmake --build build --target draxul draxul-tests` followed by `py do.py smoke` (or `python do.py smoke` on Windows).
- If you touch RPC, redraw handling, or input translation, run `ctest`.
- If you touch renderer code, build the platform-specific app target and verify startup at least once.
- After implementing a user-facing feature or rendering-affecting change, run the render smoke/snapshot suite with `t.bat` or `ctest` and confirm the relevant `draxul-render-*` scenario still passes.
- When blessing render references, use `py do.py blessbasic`, `py do.py blesscmdline`, `py do.py blessunicode`, `py do.py blessligatures`, or `py do.py blessall` from the repo root.
- If you change build wiring, keep both Windows and macOS paths valid in CI.
- Do not run `clang-format` manually in this repo. The pre-commit hook runs `clang-format` automatically on staged files, so if formatting is needed the first commit attempt may fail; re-stage the hook's edits and retry the commit.
- When you complete a work item from `plans/work-items/*.md`, tick the completed entries and move it to `plans/work-items-complete/`.
- After implementing a new user-facing feature, configuration option, CLI flag, or build/CI change, update `docs/features.md`.
- Before creating new work items, check `docs/features.md` to verify the proposed feature or capability is not already implemented.

## Platform

- **Windows**: MSVC/Visual Studio 2022. Process spawning via `CreateProcess` with piped stdin/stdout. Built as a Windows GUI app (`WIN32_EXECUTABLE`).
- **macOS**: Clang/Xcode. Process spawning via `fork()`/`exec()` with `pipe()`. Rendering via Metal.

## Known Pitfalls

- Do not include backend-private renderer headers from `app/`.
- Keep shutdown paths non-blocking; a stuck Neovim child must not hang the UI on exit.
- Font-size changes must relayout existing grid geometry even before Neovim acknowledges a resize.
- Unicode rendering is still cell-oriented. Be careful when changing shaping or grid-line parsing because combining clusters and wide glyphs are easy to regress.
- Never duplicate a header between `src/` and `include/draxul/`. Each header lives in exactly one place: public API headers under `include/draxul/` (included with angle brackets), internal headers under `src/` (included with quotes).

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
