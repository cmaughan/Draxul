# Draxul

[![Build](https://github.com/cmaughan/Draxul/actions/workflows/build.yml/badge.svg)](https://github.com/cmaughan/Draxul/actions/workflows/build.yml)

API docs: **[chrismaughan.com/Draxul](http://chrismaughan.com/Draxul/)** — or generate locally with `python scripts/gen_api_docs.py`.

## What is Draxul?

An experimental **Dark Factory Agentic** project with deep visualization of generated code.

- **GPU-accelerated terminal host** — cross-platform (Vulkan on Windows, Metal on macOS) terminal for PowerShell, WSL, Bash, Zsh, Git, and more
- **Neovim GUI frontend** — a full-featured replacement for nvim-qt, with deep `nvim --embed` integration over msgpack-RPC
- **Agentic shell server** — a per-user server owns the shells, Spaces, and agents; GPU clients attach, detach, and reconnect without losing anything
- **Plugin host** — a versioned C plugin ABI hands raw Vulkan/Metal frames to dynamically loaded product plugins, with Kanban and Markdown panes built in

This repository is the terminal / agentic / host core. The larger GPU products
are native plugins in their own repositories, mounted here as git submodules
under `plugins/` and loaded at runtime over the C ABI:

- **[draxul-megacity](https://github.com/cmaughan/draxul-megacity)** — the interactive city view of a codebase: a living 3D city where buildings represent code, with live performance and coverage overlays. The city is a human metaphor for the code an agent is building. (Richard Wettel tried this back in 2007 — https://wettel.github.io/codecity.html — we are giving it another go, plus a BioView organism mode.)
- **[draxul-satview](https://github.com/cmaughan/draxul-satview)** — satellite and sky visualization: SGP4-propagated CelesTrak catalogs on a 3D globe, Hipparcos starfield, ephemeris Moon/Sun/planets, HDR atmosphere.
- **[draxul-scoreview](https://github.com/cmaughan/draxul-scoreview)** — MusicXML piano practice: Verovio notation, MIDI and microphone judging, and an adaptive practice stream.
- **[draxul-rezonality](https://github.com/cmaughan/draxul-rezonality)** — fault-tolerant live Vulkan/Metal graphics: watched shaders and scenegraphs, multipass surfaces, OBJ/glTF models, cameras, PBR materials, and HDR environments rendered inside a pane.

**None of the code has been human-written.** Draxul is 100% agentically coded using multiple agents on Claude, Codex, and Gemini. Code reviews, feature updates, and planning are managed by agents with a human arbiter. A part-time project, built in less than 3 weeks at time of writing — several person-years of equivalent effort.

### macOS

![Draxul on macOS](screenshots/terminals_mac.png)

### Windows

![Draxul on Windows](screenshots/draxul_pc.png)

## Gallery

_Click any image to view full size._

<table>
<tr>
<td align="center"><a href="screenshots/terminals_mac_2.png"><img src="screenshots/terminals_mac_2.png" width="400"/></a><br><em>Terminal host — multi-pane with htop</em></td>
<td align="center"><a href="screenshots/draxul-overlay-mac.png"><img src="screenshots/draxul-overlay-mac.png" width="400"/></a><br><em>Diagnostics panel</em></td>
</tr>
</table>

Product visuals live with their plugins: the city and coverage galleries are in
[draxul-megacity](https://github.com/cmaughan/draxul-megacity#gallery), and
SatView / ScoreView imagery belongs to
[draxul-satview](https://github.com/cmaughan/draxul-satview) and
[draxul-scoreview](https://github.com/cmaughan/draxul-scoreview).

## Features

For the full user-facing feature reference — config options, keybindings, terminal behaviour, mouse support, scrollback, and more — see **[docs/features.md](docs/features.md)**. Product plugin documentation lives in each plugin's own repository ([draxul-megacity](https://github.com/cmaughan/draxul-megacity), [draxul-satview](https://github.com/cmaughan/draxul-satview), [draxul-scoreview](https://github.com/cmaughan/draxul-scoreview)).

- **Terminal emulator** — run `zsh`, `bash`, `powershell`, or any shell; cross-platform
- **Neovim GUI** — full ext_linegrid UI with deep Neovim integration
- FreeType + HarfBuzz text pipeline with a dynamic glyph atlas
- Font fallback for Nerd Font, emoji, and plugin glyph coverage
- Configurable GUI shortcuts for the diagnostics panel, clipboard copy/paste, and font zoom
- Mouse input support for click, drag, and wheel events
- HiDPI / Retina-aware rendering with correct DPI font scaling
- Shared logging with console/file fallback and category filtering
- Thin app layer with separate window, renderer, font, grid, and Neovim modules
- Built-in diagnostics panel (toggle with `F12`) with live renderer, layout, and startup timing

## Requirements

### Windows

- CMake 3.25+
- Visual Studio 2022
- Vulkan SDK with `glslc`
- `nvim` on `PATH` (optional — required only for Neovim mode)

### macOS

- CMake 3.25+
- Xcode Command Line Tools
- `nvim` on `PATH` (optional — required only for Neovim mode)

All other dependencies are fetched automatically with CMake `FetchContent`.

## Building

Clone with submodules — the product plugins (MegaCity, SatView, ScoreView)
live in their own repositories mounted under `plugins/`:

```bash
git clone --recurse-submodules https://github.com/cmaughan/Draxul
```

In an existing checkout, `git submodule update --init` fetches them. A checkout
without the submodules still configures and builds — you get the core terminal
with Kanban/Markdown and no product plugins.

### Windows

The recommended path is `do.py`; Debug + Ninja is the default development cache:

```powershell
py do.py build debug
py do.py run debug
py do.py test debug
```

Use Release for the final build/startup confirmation:

```powershell
py do.py run release
```

### macOS

Debug development and the final Release confirmation use the same commands:

```bash
python3 do.py build debug
python3 do.py run debug
python3 do.py test debug
python3 do.py run release
```

## Running

With no arguments, Draxul discovers or starts the per-user Draxul server and opens
its shared default shell Session. Closing the UI leaves the server, shells, Spaces,
and agents running; the next UI reconnects to them. Core hosts use `--host`;
standalone products are created as plugin panes or tabs through the shared server.

### Windows

```powershell
.\build-ninja-release\draxul.exe                         # shared PowerShell Session
.\build-ninja-release\draxul.exe --host nvim             # embedded Neovim
.\build-ninja-release\draxul.exe --host powershell       # shared PowerShell Session
.\build-ninja-release\draxul.exe plugin get dev.draxul.megacity --json
.\build-ninja-release\draxul.exe tab create --space <space-id> --name MegaCity --plugin dev.draxul.megacity --plugin-config '{"mode":"city","source":"C:/dev/linux"}' --json
.\build-ninja-release\draxul.exe --server-status
.\build-ninja-release\draxul.exe --list-sessions          # live server Sessions
.\build-ninja-release\draxul.exe --delete-session --session work --yes
.\build-ninja-release\draxul.exe --shutdown-server --yes # confirms live-shell shutdown
```

The server also exposes a Windows notification-area status menu with Open Draxul,
status counts, log access, and guarded stop actions.

### macOS

```bash
./build/draxul.app/Contents/MacOS/draxul                # shared login-shell Session
./build/draxul.app/Contents/MacOS/draxul --host nvim    # embedded Neovim
./build/draxul.app/Contents/MacOS/draxul --host zsh     # shared Zsh Session
./build/draxul.app/Contents/MacOS/draxul tab create --space <space-id> --name BioView --plugin dev.draxul.megacity --plugin-config '{"mode":"biology","source":"/Users/me/dev/linux"}' --json
```

Or launch via Finder / `open`:

```bash
open ./build/draxul.app
```

Supported `--host` values include `nvim`, `markdown`, `kanban`, `zsh`, `bash`,
`powershell` / `pwsh` (Windows), and `wsl` (Windows). MegaCity and BioView use
the stable plugin ID `dev.draxul.megacity`; `mode` selects the view and `source`
sets the local Tree-sitter scan root.

## Configuration

Draxul stores user settings in a `config.toml` file under the platform app-config directory.

Programming ligatures are enabled by default. Set `enable_ligatures = false` if you prefer raw per-character glyphs.

GUI-level shortcuts can be remapped under a `[keybindings]` table:

```toml
enable_ligatures = true

[keybindings]
toggle_diagnostics = "F12"
copy = "Ctrl+Shift+C"
paste = "Ctrl+Shift+V"
font_increase = "Ctrl+="
font_decrease = "Ctrl+-"
font_reset = "Ctrl+0"
```

These bindings affect only Draxul-handled GUI actions. Normal Neovim input and keymaps still go through Neovim.

## Convenience Script: `do.py`

The root `do.py` script is the recommended entry point for common tasks:

```bash
./do.py run          # build (if needed) and launch Draxul
./do.py test         # Debug build + parallel core suite in the same cache
./do.py test --satview  # Add SatView's test suite
./do.py test --products # Add every product test suite
./do.py test --all      # Complete unit inventory
./do.py smoke --skip-build # reuse that built app for the startup smoke test
./do.py clean        # remove repository-root build/ and build-* directories

./do.py basic        # run basic-view render snapshot compare
./do.py cmdline      # run cmdline-view render snapshot compare
./do.py unicode      # run unicode-view render snapshot compare
./do.py panel        # run panel-view render snapshot compare
./do.py renderall    # run all four render snapshot compares

./do.py blessbasic   # bless basic-view reference image
./do.py blesscmdline # bless cmdline-view reference image
./do.py blessunicode # bless unicode-view reference image
./do.py blesspanel   # bless panel-view reference image
./do.py blessall     # bless all four reference images

./do.py api          # build local Doxygen API docs
./do.py docs         # build all documentation artifacts
./do.py shot         # regenerate the README hero screenshot
./do.py coverage     # macOS: export build/coverage.lcov and refresh db/coverage.lcov
```

On Windows, use `py do.py <command>`.

```bash
py do.py build relwithdebinfo  # Windows: optimized build with PDB symbols
py do.py run                   # Debug build + run (Ninja on Windows)
py do.py test                  # Same Debug cache + parallel core suite
py do.py test --scoreview      # Core + ScoreView suites
py do.py test --products       # Core + every product suite
py do.py test --all            # Complete unit inventory
py do.py smoke --skip-build    # Startup check without another build
py do.py run release           # Final Release build + startup confirmation
py do.py run release --vs      # Explicit VS-generator check when needed
py do.py run --console         # Attach a debug console (Windows)
py do.py clean                 # Remove build/ and build-*/
```

`do.py clean` removes repository-root build trees named `build/` or `build-*` (including the Visual Studio, Ninja, and tooling build directories). It leaves deploy packages, render outputs, render references, databases, source files, and similarly named regular files untouched; running it when no matching build directory exists succeeds.

## Testing

The repository includes lightweight native tests for grid logic, redraw parsing, input translation, RPC behavior, renderer state, and Unicode width conformance against headless Neovim.

For the normal edit-test loop, `do.py build`, `do.py run`, and `do.py test` share the same selected generator, configuration, and build tree. They default to Debug and Ninja on Windows. `do.py test` builds the core test aggregate and its dependencies—including the app required by the core test contract—then runs only the core, app, Markdown/Kanban, and Python workflow tests with bounded parallelism. It does not launch the app or run smoke/render comparisons.

Product tests are opt-in and additive: use `--megacity`, `--satview`, or `--scoreview` when changing that product or a shared seam it consumes. Use `--products` for changes to shared plugin SDK/support/renderer code that can affect every product. `--all` builds the historical `draxul-tests` aggregate and runs the complete unit inventory. The individual product flags and `--products` keep the build scoped to their named aggregates; `--all` is the explicit broad acceptance path.

Before committing, run `do.py test debug` with any relevant product flag, then `do.py smoke --skip-build` so smoke reuses the app already produced in that cache. Run only the relevant render shortcut for rendering-affecting changes. Finish a completed feature or bug fix with `do.py run release` and confirm startup; headless automation can use `do.py run release --console -- --smoke-test`.

On Windows, a long-lived Draxul server keeps its selected cache's `draxul-server.exe` open. If the Debug app has since been relinked, run process-launch tests before starting that server or safely stop that exact server after checking for connected clients and live terminals. Do not switch the entire test pass to another generator merely to avoid the helper lock.

The `t.sh`, `t.bat`, and `scripts/run_tests.*` wrappers remain available for an explicitly requested broad or multi-configuration validation pass. They are not the normal edit-build-test path and should not be stacked with equivalent `do.py` validation.

### Windows

Default is `Debug`:

```powershell
scripts\run_tests.bat
```

Other modes:

```powershell
scripts\run_tests.bat release
scripts\run_tests.bat both
scripts\run_tests.bat --reconfigure
scripts\run_tests.bat --verbose
scripts\run_tests.bat --unit
```

### macOS

Default is `Debug`:

```bash
./scripts/run_tests.sh
```

Other modes:

```bash
./scripts/run_tests.sh release
./scripts/run_tests.sh both
./scripts/run_tests.sh --reconfigure
./scripts/run_tests.sh --verbose
./scripts/run_tests.sh --unit
```

The test scripts reuse the existing CMake cache when possible and only reconfigure when needed. Their default is broad validation; `--unit` still selects the complete CTest unit inventory, whereas the normal `do.py test` path is core-scoped unless a product flag is supplied. By default they use failure-only CTest output so CI logs stay readable; pass `--verbose` when you want full per-test output locally.

The CTest suite also includes:

- an app startup smoke test when `nvim` is available on `PATH`
- a render snapshot regression test when the platform reference image exists under `tests/render/reference/`

## Render Snapshots

Draxul can now run deterministic render-snapshot tests by capturing pixels directly from the renderer output instead of taking a desktop screenshot.

Example compare run:

```powershell
.\build\Debug\draxul.exe --console --render-test D:\dev\draxul\tests\render\basic-view.toml
```

Bless a new reference image:

```powershell
.\build\Debug\draxul.exe --console --render-test D:\dev\draxul\tests\render\basic-view.toml --bless-render-test
```

Update the documentation screenshot for the current platform:

```powershell
python .\scripts\update_screenshot.py
```

Notes:

- `update_screenshot.py` uses the presentation-oriented `tests/render/readme-hero.toml` scenario by default, so it captures your normal Neovim theme and statusline instead of the clean `-u NONE --noplugin` regression setup.
- The deterministic render regression scenarios remain under `tests/render/` and continue to use fixed startup settings for stable compare/bless behavior.

Behavior:

- the scenario fixes window size, font, and Neovim startup commands
- Draxul waits for redraw activity to settle
- the renderer reads back the presented frame
- output is compared against a platform-specific reference image
- `actual`, `diff`, and `report` artifacts are written under `tests/render/out/`

Reference images live under `tests/render/reference/` with platform suffixes like `basic-view.windows.bmp` and `basic-view.macos.bmp`.

Current scenarios:

- `basic-view`: line numbers, signcolumn, cursorline, and baseline text layout
- `cmdline-view`: bottom-row command-line rendering
- `unicode-view`: graphemes, emoji, wide glyphs, and Nerd Font/plugin icons
- `panel-view`: visible diagnostics panel layout and content

## Logging

Draxul now uses a shared repo-local logger across the app, RPC/process layer, windowing, font stack, and renderers.

Environment controls:

```powershell
$env:DRAXUL_LOG = "debug"
$env:DRAXUL_LOG_CATEGORIES = "app,rpc,font"
$env:DRAXUL_LOG_FILE = "logs\\draxul.log"
```

Notes:

- Default level is `info`.
- Categories are comma-separated.
- GUI launches without a console will fall back to a log file automatically.
- The DPI diagnostics in the window layer are now `debug`-only instead of always-on.

## Diagnostics Panel

Press `F12` to toggle the built-in diagnostics panel. This can be remapped via `config.toml` under `[keybindings]`.

The panel is a bottom-aligned dockable window rendered at native physical-pixel resolution using the same font as the terminal. It exposes live runtime state across three tabs:

**Window** — window and terminal region dimensions, display DPI, cell size, and grid dimensions

**Renderer** — last frame time, rolling average frame time, dirty-cell count, and glyph atlas occupancy, count, and reset statistics

**Startup** — per-phase initialisation timing (Config, Window + Renderer, Font, ImGui, Host) and total wall-clock time

The panel does not intercept any keyboard input — the terminal remains fully interactive while it is visible.

## Project Layout

```text
draxul/
├── app/                    # App startup and main orchestration
├── libs/
│   ├── draxul-types/      # Shared POD types and event structs
│   ├── draxul-window/     # Window abstraction and SDL implementation
│   ├── draxul-renderer/   # Public renderer API and platform backends
│   ├── draxul-font/       # Font loading, shaping, glyph cache
│   ├── draxul-grid/       # Cell grid and highlight state
│   └── draxul-nvim/       # Neovim process, RPC, redraw handling, input
├── plugins/
│   ├── megacity/          # Submodule → github.com/cmaughan/draxul-megacity
│   ├── satview/           # Submodule → github.com/cmaughan/draxul-satview
│   ├── scoreview/         # Submodule → github.com/cmaughan/draxul-scoreview
│   ├── spinning-triangle/ # In-repo reference plugin and ABI test vehicle
│   └── support/           # Generic plugin-support code (ImGui host)
├── sdk/                    # Public C plugin ABI (Draxul::PluginSDK)
├── shaders/                # Vulkan and Metal shader sources
├── fonts/                  # Bundled font assets copied next to the app
├── tests/                  # Native test executable and fixture helpers
└── scripts/                # Build/test convenience scripts
```

For a guided human-facing overview of the repo structure, generated diagrams, and validation entry points, see [docs/module-map.md](docs/module-map.md).

## CI

GitHub Actions builds and tests the project on:

- Windows
- macOS

The workflow runs automatically for pushes and pull requests to `main`, and can also be started manually.

The workflow uses the same repo-local test scripts as local development, including the startup smoke test.

## Notes

- Windows uses a multi-config Visual Studio generator through `CMakePresets.json`.
- The renderer boundary is owned by `draxul-renderer`; app code should not include backend-private headers.
- Grapheme handling is much better than the original single-codepoint path, but broad Unicode width conformance against Neovim is still future hardening work.
- Visual regression testing now prefers direct swapchain/drawable readback over desktop screenshots so comparisons stay deterministic across window-manager state.

## Architecture Diagrams

### Architecture Overview

![Draxul architecture](docs/architecture/architecture.claude.svg)

Regenerate with the prompt in `plans/prompts/architecture_diagram.md`.

### CMake Target Dependencies

Regenerate with `python scripts/build_docs.py`.

![CMake target dependency graph](docs/deps/deps.svg)

### Class Diagram

![C++ class diagram](docs/uml/draxul_classes.svg)

### API Docs

The published API reference is available at **[chrismaughan.com/draxul](http://chrismaughan.com/draxul/)**. Generate an up-to-date local copy with `python scripts/gen_api_docs.py --output docs/api`.

To generate locally:

```bash
python scripts/gen_api_docs.py
```

This writes a local Doxygen site to `docs/api/index.html`.

## Unicode Snapshot Example

Reference image:

![Unicode render reference](tests/render/reference/unicode-view.windows.bmp)

What the render smoke does:

- launches a deterministic Neovim UI scenario at a fixed size with fixed fonts and commands
- waits for redraw activity to settle instead of capturing a half-initialized frame
- reads pixels back from the renderer output directly, not from the desktop compositor
- compares the captured image against a blessed platform reference
- writes `actual`, `diff`, and `report` artifacts under `tests/render/out/`

Why this is useful:

- it catches visual regressions that ordinary unit tests miss, such as tofu, broken fallback fonts, missing line numbers, layout shifts, or highlight mistakes
- the `diff` artifact makes it obvious what changed and roughly how much changed
- the `report` gives a mechanical pass/fail threshold instead of relying on guesswork
- `--bless-render-test` gives a controlled way to accept intentional visual changes
