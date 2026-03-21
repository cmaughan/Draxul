# Draxul Agent Guide

## Scope

This repository is a cross-platform Neovim GUI frontend with:

- Vulkan rendering on Windows
- Metal rendering on macOS
- SDL3 windowing/input
- msgpack-RPC communication with `nvim --embed`

The codebase is intentionally split into small libraries. Keep app code thin and push platform or subsystem logic downward into `libs/`.

## Build And Test

### Windows

Requirements:

- CMake 3.25+
- Visual Studio 2022
- Vulkan SDK with `glslc`
- `nvim` on `PATH` for runtime checks

Commands:

```powershell
cmake --preset default
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure
```

Run:

```powershell
.\build\Release\draxul.exe
.\build\Release\draxul.exe --console
```

### macOS

Requirements:

- CMake 3.25+
- Xcode Command Line Tools
- `nvim` on `PATH` for runtime checks

Commands:

```bash
cmake --preset mac-debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run:

```bash
./build/draxul
```

## Architecture

- `libs/draxul-types`: shared POD types and event structs
- `libs/draxul-window`: `IWindow` abstraction and SDL implementation
- `libs/draxul-renderer`: `IRenderer` abstraction, backend factory, Vulkan and Metal backends
- `libs/draxul-font`: FreeType/HarfBuzz font loading, shaping, glyph atlas management
- `libs/draxul-grid`: terminal grid model, dirty tracking, highlights
- `libs/draxul-nvim`: embedded Neovim process, RPC transport, redraw parsing, input translation
- `app/`: orchestration only

Preferred dependency direction:

- `app` depends on public headers only
- renderer backends stay private to `draxul-renderer`
- pure logic stays testable without launching Neovim or opening a window

## Validation Expectations

- **Always build and run the smoke test before committing.** Use `cmake --build build --target draxul draxul-tests` followed by `py do.py smoke` (or `python do.py smoke` on Windows). This catches broken includes, link errors, and basic startup failures that only surface after merging changes from multiple sources.
- If you touch RPC, redraw handling, or input translation, run `ctest`.
- If you touch renderer code, build the platform-specific app target and verify startup at least once.
- After implementing a user-facing feature or rendering-affecting change, run the render smoke/snapshot suite with `t.bat` or `ctest` and confirm the relevant `draxul-render-*` scenario still passes.
- When blessing render references, use `py do.py blessbasic`, `py do.py blesscmdline`, `py do.py blessunicode`, `py do.py blessligatures`, or `py do.py blessall` from the repo root instead of calling `draxul.exe --render-test` manually. The helper passes repo-rooted scenario paths and avoids working-directory mistakes.
- If you change build wiring, keep both Windows and macOS paths valid in CI.
- After every completed work item, run one final `clang-format` pass across all touched source files in a single shot instead of formatting piecemeal during the work. Note: the pre-commit hook runs `clang-format` automatically on staged files, so a commit that needs reformatting will be rejected on the first attempt — simply re-stage and retry the commit.
- When you complete a work item or a concrete subtask from `plans/work-items/*.md`, update that markdown file in the same turn and mark the completed entries with Markdown task ticks (`- [x]`). Leave incomplete follow-ups as unchecked items so progress stays visible in the file itself.
- When a work item from `plans/work-items/*.md` is fully complete, move it to `plans/work-items-complete/` in the same turn and update any index/reference links that still point at the old location.

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

## Known Pitfalls

- Do not include backend-private renderer headers from `app/`.
- Keep shutdown paths non-blocking; a stuck Neovim child must not hang the UI on exit.
- Font-size changes must relayout existing grid geometry even before Neovim acknowledges a resize.
- Unicode rendering is still cell-oriented. Be careful when changing shaping or grid-line parsing because combining clusters and wide glyphs are easy to regress.
- Never duplicate a header between `src/` and `include/draxul/`. Each header lives in exactly one place: public API headers under `include/draxul/` (included with angle brackets), internal headers under `src/` (included with quotes). Maintaining two copies causes them to diverge silently and is flagged by static analysis (SonarCloud).
## Consensus Shortcut

When the user says `come to consensus`, treat that as a direct instruction to execute the saved consensus prompt in `plans/prompts/consensus_review.md`.

## Prompt History

When the user asks to store prompts from the current thread, write them to a dated markdown file under `plans/prompts/history/` in chronological order and mark interrupted or partial prompts inline.
