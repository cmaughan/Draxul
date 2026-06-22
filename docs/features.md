# Draxul Features

Quick reference of all user-facing features, configuration, CLI flags, build options, and CI infrastructure.

---

## Host Types

| Host | Flag | Description |
|------|------|-------------|
| Neovim | `--host nvim` (default) | Embeds `nvim --embed` via msgpack-RPC over stdin/stdout pipes |
| Markdown | `--host markdown --source <file.md>` | Native Draxul markdown viewer host using the FreeType/HarfBuzz font pipeline, MD4C parsing, variable-height document rows, configurable body text size/margins, restrained styled headings, section indentation, front matter/code/list/table decorations, mouse wheel/PageUp/PageDown/Home/End plus Vim-style `j/k`, `Ctrl+F/B`, `gg`, `G` scrolling, and a draggable proportional scrollbar |
| Kanban | `--host kanban [--source <folder>]` | Native grid-backed kanban viewer for a `kanban/` folder. Subfolders become columns, Markdown files become cards, `.draxul-kanban.toml` stores ordering, Vim-style `h/j/k/l` moves selection, shifted movement reorders cards or moves files between column folders, and Enter opens the selected card in a Markdown pane |
| Bash | `--host bash` | PTY-based terminal (Unix) |
| Zsh | `--host zsh` | PTY-based terminal (Unix) |
| PowerShell | `--host powershell` | ConPTY on Windows, PTY on macOS/Linux |
| WSL | `--host wsl` | Windows Subsystem for Linux shell |
| MegaCity | `--host megacity` | 3D demo host (semantic code city, textured road/sidewalk/tree materials, cascaded directional shadows, point-light cubemap shadows, screen-space AO, mouse-drag pan, Alt+drag orbit, direct Tree-sitter semantic source, optional `--source` Tree-sitter scan-root override, optional Graphify graph source) |

Pane splits use the platform default shell (Zsh on macOS, PowerShell on Windows) regardless of primary host type.

---

## Rendering

- **Backends**: Vulkan (Windows), Metal (macOS)
- **Renderer target layout**: Public `draxul-renderer` API stays stable while the build internally splits shared renderer core and platform backend implementation targets
- **Architecture**: Two-pass instanced draw -- background quads then alpha-blended foreground glyphs
- **Glyph atlas**: Configurable size (default 2048x2048 RGBA8), shelf-packed, incremental upload
- **Buffer**: Host-visible/shared memory, direct writes, no staging. 112 bytes per cell
- **Frames in flight**: 2 with synchronization primitives
- **Pixel format**: BGRA8 Unorm (Neovim sends pre-sRGB colors)
- **MegaCity materials**: Textured asphalt road surfaces, paving-stone sidewalks, flat-color procedural n-gon building shell meshes with configurable roughness/metallic, bark-textured central-park trees, plus forward-lit material debug controls including metallic, tangent, bitangent, packed-TBN, directional-shadow, point-shadow, point-shadow-face, point-shadow-stored-depth, and point-shadow-depth-delta views
- **MegaCity surface pipeline**: Opaque MegaCity rendering now uses cascaded directional shadow maps, point-light cubemap shadow maps, a depth/normal AO prepass, an offscreen MSAA depth buffer, an MSAA `RGBA16F` scene color target, a resolved HDR scene texture, and a final `BGRA8 sRGB` scene texture before the main swapchain present; the debug panel can inspect the resolved HDR/final scene targets, directional shadow cascades, and point-shadow faces alongside the AO/GBuffer surfaces
- **MegaCity tone mapping controls**: The HDR post pass now applies tone mapping before the final sRGB target, with configurable `Exposure` and `White Point` controls in the Megacity lighting UI
- **MegaCity module surfaces**: Each non-central module now draws a thin module-colored outline above the shared road layer so module footprints are readable beneath sidewalks and buildings
- **MegaCity park dressing**: Central park now includes a procedurally generated `DraxulTree` mesh with atlas-based PBR leaf cards
- **MegaCity dependency routing**: The City Map panel now overlays routed building-to-building dependency lines driven by Tree-sitter field references and road-only semantic routing, and the same routed polylines are emitted into the 3D scene as thin raised connection strips with a directional green-to-red gradient from source to target, plus a configurable per-route layer step for stacked overlap readability
- **MegaCity semantic filters**: The City Build UI can now hide test entities and struct-backed entities before layout/build
- **MegaCity stacked struct plates**: Same-footprint structs within a module are stacked vertically into compact square-section plate buildings with configurable gap, max-per-stack, and sign colors; each plate remains independently clickable with full dependency routing and per-plate tooltips
- **MegaCity building shading controls**: The City Build UI includes `Middle Strip Push`, `Alternate Darken`, `Flat Roughness`, and `Flat Metallic` controls for non-textured procedural buildings, so flat-color shells can get configurable per-level mid-band ripples, alternating-band darkening, roughness, and metallic without affecting roads, routes, signs, or other flat overlays
- **MegaCity projection toggle**: The renderer panel can switch the MegaCity camera between `Orthographic` and `Perspective`; the choice persists in config, keeps the existing orbit/pan/zoom interactions, and also drives perspective-aware cascade splits and screen-space zoom scaling
- **MegaCity code source modes**: The City Build UI can build the semantic city directly from a completed Tree-sitter scan or directly from a Graphify graph JSON. The Tree-sitter mode projects scanner output into semantic city records in memory before layout; the old SQLite city snapshot module has been removed. Graphify mode defaults to `graphify-out/graph.json` and still feeds the same semantic-city builder, module filtering, dependency routing, labels, and metrics pipeline
- **MegaCity performance preview and coverage modes**: The Codebase Analysis panel now exposes saved top-level `Perf`, `Coverage`, `LCOV Coverage`, and `Perf Log Scale` controls. `Perf` blends flat-color buildings toward a green-to-red heat palette per semantic building layer using smoothed live timing heat, while `Coverage` forces any touched/matched function layer to full heat so executed code lights up clearly. `LCOV Coverage` imports a static LLVM `lcov` tracefile from `db/coverage.lcov` or `build/coverage.lcov` and lights semantic function layers based on function-level test coverage from the LLVM coverage report — covered functions render as hot, uncovered stay at base color. The local `do.py coverage` flow exports `build/coverage.lcov` and refreshes `db/coverage.lcov` for app use. The debug panel shows LCOV-specific diagnostics (report functions, covered functions, matched/heated layers/buildings), and the building tooltip reports per-function coverage status. `Perf Log Scale` applies a visual logarithmic boost to low heat values so more active layers move toward the warm end without changing the underlying timing data. All modes are driven by a live or imported metrics snapshot for every building and function, indexed in the shader by stable building/layer ids, and accompanied by an in-panel matched/unmatched perf debug readout plus tooltip timing details for hovered functions
- **MegaCity sign sizing controls**: Building roof-sign rings can now enforce a configurable `Min Width / Char`, so long class/module labels can expand the repeated sign band instead of being squeezed into the default building footprint
- **MegaCity building shape thresholds**: The City Build UI now exposes both `Hex Threshold` and `Oct Threshold`, letting connected buildings step from 4-sided to 6-sided to 8-sided procedural shells based on total incident dependency count
- **MegaCity selection tuning**: Selection fade now has configurable dependency, hidden, hover-hidden, and road hidden alpha controls, with configurable spacebar-held raise/fall timing for hidden buildings so the shared road layer can remain fully visible while selected-context buildings read clearly
- **Markdown viewer pipeline**: Markdown panes are rendered by Draxul itself rather than through the terminal grid or ImGui. The host parses Markdown into document blocks, lays them out as variable-height rows, builds a GPU draw list of styled rectangles and glyph runs, uploads rich-text atlas regions incrementally, and renders directly through the platform hardware renderer. GitHub/Obsidian pipe tables render with header/body styling, cell borders, wrapped cell text, left/center/right column alignment, and content-aware column widths that balance required and preferred cell sizes. Markdown body size is controlled independently through `[markdown].font_size`, headings scale relative to it, focused Markdown panes consume `font_increase`, `font_decrease`, and `font_reset`, and `[markdown].margin_columns` controls the document margin in body character widths. Navigation supports PageUp/PageDown/Home/End, wheel scrolling, Vim-style `j/k`, `Ctrl+F/B`, `gg`, `G`, and mouse dragging on the wider scrollbar thumb.

## GUI (draxul-gui)

A standalone GUI library for rendering UI items that do not depend on ImGui. It leverages the project's font engine and GPU renderer for high-performance, pixel-precise overlays.

- **Tooltips**: Multi-line tooltips with a semi-transparent dark background and a 2-column table layout for labels and values. Rasterized on-demand via `TextService` and rendered as a screen-space alpha-blended quad.
- **Toast notifications**: Auto-dismissing notifications stacked at the bottom-right corner via `ToastHost` (info/warn/error levels with distinct colors and fade-out animation). Thread-safe `push()` and `IHostCallbacks::push_toast()` lets any host or app subsystem report recoverable failures (clipboard errors, font fallback warnings, unknown config keys, secondary host spawn failures, invalid pane targets) without blocking the user. Toasts pushed before the host exists during init are queued and replayed.
- **Shaders**: Generic `gui_tooltip.vert/frag` (Vulkan) and `gui.metal` (Metal) for rendering GUI elements.

---

## Font Pipeline

- **FreeType** loads faces, **HarfBuzz** shapes text, glyph cache rasterizes on demand
- **Ligatures**: Programming ligatures via HarfBuzz (configurable, default on); supports multi-cell ligatures up to 6 cells (e.g. `===`, `!==`, `>>=`, `<<=`), with correct highlight-boundary breaking. Ligature spans cover only the cells whose shaping actually changed, cluster glyphs are pinned to grid-cell pitch, and edits regroup the whole shaping run — so incremental typing produces pixel-identical output to a full repaint
- **Multi-weight**: Bold, italic, bold+italic via separate font files
- **Fallback chain**: Primary font + configurable fallback paths for missing glyphs. macOS defaults include STIX Two Math for technical symbols (e.g. `⏵` U+23F5) absent from Apple Symbols
- **Synthesized box drawing**: Box Drawing (U+2500–257F) and Block Elements (U+2580–259F) are drawn procedurally at exact cell size instead of rasterized from the font, so adjacent cells tile seamlessly at any size/DPI (no anti-aliased gaps in TUI borders, progress bars, or logos)
- **Emoji**: Color glyph rendering, variation selectors (VS-16), ZWJ sequences
- **Wide characters**: CJK double-width, combining characters
- **Bundled fonts**: JetBrains Mono Nerd Font (regular/bold/italic/bold-italic), Cascadia Code
- **Rich text service**: Markdown viewing can resolve separate point sizes and bold/italic style keys through pooled `TextService` instances, enabling larger heading rows without forcing the terminal grid to adopt variable-sized cells.

---

## Terminal Emulation (shell hosts)

- **VT100+** escape sequence support (ANSI/256/24-bit SGR colors, cursor control, DECSTBM scroll regions)
- **Scrollback**: Configurable row ring buffer with viewport offset (default 10000)
- **Alt screen**: Main/alt switching with snapshot restore
- **Mouse modes**: None, button-click, drag, all-motion (SGR encoding)
- **xterm focus reporting**: DECSET `?1004` emits `CSI I` / `CSI O` on pane focus gain/loss
- **DEC special graphics / ACS**: `ESC ( 0`, `ESC ) 0`, `SO`, and `SI` map VT line-drawing characters to Unicode box-drawing glyphs
- **Bracketed paste**: VT-wrapped clipboard paste
- **Paste confirmation**: Pastes ≥ `paste_confirm_lines` newlines stash the payload and surface a toast; `confirm_paste` (default `Ctrl+Shift+Enter`) sends it, `cancel_paste` (default `Ctrl+Shift+Escape`) discards it. Set `paste_confirm_lines = 0` to disable
- **OSC 7**: Current working directory tracking from shell
- **OSC 8**: Terminal hyperlink regions are tracked per grid cell, underlined, and open on click
- **OSC 52**: Clipboard read (`?` query) and write (base64 payload) for tmux/SSH/Neovim remote clipboard integration
- **URL detection**: HTTP/HTTPS text is underlined and can be opened with Ctrl/Cmd-click; explicit OSC 8 hyperlinks take priority
- **Shell TERM identity**: Unix PTY shell hosts advertise `TERM=xterm-256color`, `COLORTERM=truecolor`, and `TERM_PROGRAM=draxul`
- **Selection**: Click-and-drag with system clipboard integration; configurable cell cap (`selection_max_cells`, default 65536)
- **Word/line selection**: Double-click selects the word at the cursor (contiguous non-whitespace), triple-click selects the entire row
- **Selection copy gestures**: Clicking inside an existing mouse selection copies it to the system clipboard; `Ctrl+C` also copies when a shell-pane mouse selection is active, without sending SIGINT to the process
- **Copy on select**: `copy_on_select` automatically copies completed mouse selections (drag, double-click, or triple-click) to the system clipboard; enabled by default
- **Keyboard copy mode**: `toggle_copy_mode` (default `Ctrl+S, Return`) enters a vim/tmux-style cursor: `h/j/k/l` and arrows move, `0/Home/End` jump to line bounds, `g/Shift+G` jump to top/bottom, `v`/`V` start char/line selection, `y` yanks to clipboard and exits, `Esc`/`q` exits without copy. Available on shell hosts only (Neovim panes already provide their own visual mode)
- **Terminal colors**: Configurable foreground/background via `[terminal]` config section

---

## Input

- **Keyboard**: Full SDL3 key events with modifier tracking (shift, ctrl, alt, super)
- **IME**: Text input + text editing event forwarding
- **Mouse**: Button, motion, wheel with per-host protocol routing
- **MegaCity camera**: Left-drag in the render view pans the scene, `Alt` + left-drag scrubs orbit
- **Smooth scroll**: Trackpad momentum accumulation (configurable speed multiplier)
- **File drop**: Native drag-and-drop dispatched to host as `open_file:` action
- **Kanban navigation**: Kanban panes support Vim-style card selection with `h/j/k/l`, shifted `H/J/K/L`/arrow movement for reordering cards and moving files between columns, `r` reload, and Enter to open the selected Markdown card.
- **GUI keybindings**: Chord-style prefix bindings (e.g. `ctrl+s, |`)
- **Command palette**: `Ctrl+Shift+P` opens a centered fuzzy-search overlay for all GUI actions with fzf-style scoring, `Ctrl+J/K` navigation, keybinding hints, and palette-rendered text prompts for actions needing short values
- **Config reload**: `reload_config` rereads `config.toml` on demand so palette alpha, keybindings, scroll settings, ligatures, terminal font changes, and Markdown font/margin changes can be applied without a restart

---

## Split Panes

- Binary split tree with vertical and horizontal splits
- Draggable dividers with ratio-based sizing — hovering a divider switches the mouse cursor to the platform EW/NS resize cursor; click-and-drag updates the ratio in real time
- Per-pane host instance with independent lifecycle
- Focus tracking and pane-aware input routing
- Keyboard-driven pane focus navigation (`Ctrl+H/J/K/L` vim-style) via `focus_left`, `focus_right`, `focus_up`, `focus_down` actions
- Keyboard-driven pane resizing via `resize_pane_left`, `resize_pane_right`, `resize_pane_up`, `resize_pane_down` actions (each nudges the nearest enclosing divider by 5%)
- **Pane zoom**: `toggle_zoom` action (default `Ctrl+S, z`) expands the focused pane to fill the full window; toggling again restores the previous split layout exactly (like tmux `Ctrl+B z`)
- **Close pane**: Closes the focused pane and its host; if last pane, exits the app
- **Shell session restore from saved topology**: Normal desktop launches save shell-session tabs, split layout, focus, pane names, tab names, launch commands, and working directories in a local session-state file on clean shutdown, then restore that saved shell layout by respawning panes on the next launch. This is still shell-host only and not full crash recovery yet.
- **Opt-in shell session detach/reattach**: `--persistent-app` restores the old live background behavior: closing the main window hides Draxul and keeps shell-pane workspaces alive, and a later launch with `--persistent-app` reattaches to that existing instance instead of starting a second process.
- **Session-scoped shell restore CLI/UI**: `--session <id>` selects which saved shell session Draxul should restore, `--new-session` starts a fresh saved shell session (generating a unique id when `--session` is omitted), `--session-name <name>` sets the saved display name for a newly launched or restored session, `--rename-session --session-name <name>` renames a running or saved session, `--list-sessions` prints known sessions with live/detached/saved status and workspace/pane counts (preferring live owner summaries when available), `--persistent-app` enables live detach/reattach for desktop launches, `--attach-session` explicitly activates a running persistent app session, `--detach-session` explicitly detaches a running persistent app session without killing it, `--kill-session` explicitly kills a live session or deletes its saved topology, and the command palette `save_session_as` action saves the current restorable shell topology under a prompted display name and switches the running app to the generated named session id.
- **Session picker UI**: `--pick-session` opens a keyboard-driven session picker that lists known sessions, lets Enter attach or restore the selected session, lets Delete kill one, and keeps a `new-session` row at the top so typing a query becomes the name of a fresh session.
- **Abnormally exited shell panes stay inspectable**: If a shell pane dies unexpectedly, Draxul keeps the pane and its last rendered output visible instead of immediately tearing it down. The pane status pill shows `[exited]`, a toast points you at `restart_host`, and the existing restart action respawns the host in place. Clean shell exits still close the pane normally.
- **Session startup messaging**: Shell sessions surface a toast when Draxul starts a brand-new session, restores a saved topology, or reattaches to a live detached session, so the user can tell which kind of magic just happened.
- **Restart host**: Kills the current host in the focused pane and relaunches with the same arguments
- **Swap pane**: Swaps the focused pane with the next pane in spatial order

---

## Workspace Tabs

- Multiple workspaces, each with its own independent split tree and host set
- The top tab bar remains visible even with a single workspace and shows right-aligned pills for live system usage and active chord prefixes
- `new_tab` (`Ctrl+S, C`): Create a new workspace tab
- `close_tab` (`Ctrl+S, &`): Close the active workspace tab (disabled when only one tab remains)
- `next_tab` (`Ctrl+S, N`): Cycle to the next workspace
- `prev_tab` (`Ctrl+S, P`): Cycle to the previous workspace
- Tab switching preserves focus state per workspace (focus lost/gained notifications)
- **Inline tab rename**: double-click a workspace tab pill (or press `Ctrl+S, ,` — tmux-style chord) to edit the tab name in place. Enter commits, Escape cancels, Backspace/Delete/Home/End/Left/Right work as expected. Empty commits leave the existing name untouched.
- **OSC 7 default naming**: shell hosts (e.g. zsh) drive the workspace tab name from the OSC 7 working-directory escape until the user explicitly renames the tab; once the user sets a name, OSC 7 updates no longer overwrite it.
- **Inline pane rename**: double-click a pane status pill (or press `Ctrl+S, .`) to set a per-pane override name. Empty commit clears the override and reverts to the host-provided status text. Pane name overrides are in-memory only and follow the leaf for the lifetime of the session.
- **Luminance-based pill text colour**: tab and pane pill text colour is chosen automatically from the underlying NanoVG fill via BT.709 relative luminance, so any future background tweak gets a readable foreground without re-tuning a constant.

---

## Diagnostics Panel (ImGui)

Toggle with F12. Shows:

- Display DPI, cell size, grid dimensions, dirty cell count
- Frame timing (current + average)
- Atlas usage ratio and glyph count
- Startup profiling step timings
- MegaCity renderer controls, including code source selection (`Tree-sitter` or `Graphify`), Graphify graph path editing, module filtering (`All Modules` or a selected module), a `Point Shadow Debug Scene` toggle, debug views (`Final Scene`, `Ambient Occlusion`, `Normals`, `World Position`, `Roughness`, `Metallic`, `Albedo`, `Tangents`, `UV`, `Depth`, `Bitangents`, `TBN Packed`, `Directional Shadow`, `Point Shadow`, `Point Shadow Face`, `Point Shadow Stored Depth`, `Point Shadow Depth Delta`), tone-mapping controls, AO tuning, shadow-map inspection, and configurable connected-building hex/oct thresholds
- MegaCity sign styling controls, including separate module-sign and building-sign board/text colors
- MegaCity central-park tree controls, including age, seed, branch depth/count, curvature, trunk/branch wander, bend frequency/deviation, leaf density/orientation randomness, leaf size range, leaf start depth, bark colors, and atlas-based leaf cards with PBR normal/roughness/opacity/scattering textures

---

## Default Keybindings

| Action | Default Binding |
|--------|-----------------|
| `toggle_diagnostics` | `F12` |
| `toggle_host_ui` | `F1` |
| `copy` | `Ctrl + Shift + C` |
| `paste` | `Ctrl + Shift + V` |
| `font_increase` | `Ctrl + =` |
| `font_decrease` | `Ctrl + -` |
| `font_reset` | `Ctrl + 0` |
| `split_vertical` | `Ctrl + S, Shift + \` |
| `split_horizontal` | `Ctrl + S, -` |
| `command_palette` | `Ctrl + Shift + P` |
| `save_session_as` | (unbound) |
| `edit_config` | (unbound) |
| `reload_config` | (unbound) |
| `toggle_zoom` | `Ctrl + S, Z` |
| `close_pane` | `Ctrl + S, X` |
| `restart_host` | `Ctrl + S, R` |
| `swap_pane` | `Ctrl + S, O` |
| `focus_left` | `Ctrl + H` |
| `focus_down` | `Ctrl + J` |
| `focus_up` | `Ctrl + K` |
| `focus_right` | `Ctrl + L` |
| `resize_pane_left` | `Ctrl + S, Left` |
| `resize_pane_right` | `Ctrl + S, Right` |
| `resize_pane_up` | `Ctrl + S, Up` |
| `resize_pane_down` | `Ctrl + S, Down` |
| `open_file_dialog` | (unbound) |
| `new_tab` | `Ctrl + S, C` |
| `close_tab` | `Ctrl + S, &` |
| `next_tab` | `Ctrl + S, N` |
| `prev_tab` | `Ctrl + S, P` |
| `rename_tab` | `Ctrl + S, ,` |
| `rename_pane` | `Ctrl + S, .` |
| `confirm_paste` | `Ctrl + Shift + Enter` |
| `cancel_paste` | `Ctrl + Shift + Escape` |
| `toggle_copy_mode` | `Ctrl + S, Return` |
| `test_toast` | (unbound) |

Customizable in `config.toml` under `[keybindings]`. Chord syntax: `"prefix, key"`. Set to empty string to unbind. The font actions adjust the focused Markdown pane when it accepts them; otherwise they adjust the shared terminal/grid font.

---

## Configuration (config.toml)

### Display

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `window_width` | 1280 | 800--8000 | |
| `window_height` | 800 | 600--8000 | |

### Font

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `font_size` | 11.0 | 6.0--72.0 | Points; 0.5pt step on increase/decrease |
| `font_path` | (bundled) | | Primary font file path |
| `bold_font_path` | (none) | | Bold variant |
| `italic_font_path` | (none) | | Italic variant |
| `bold_italic_font_path` | (none) | | Bold + italic variant |
| `fallback_paths` | [] | | Array of fallback font paths |
| `enable_ligatures` | true | | Programming ligature combining |

### Markdown (`[markdown]` section)

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `font_size` | `font_size` | 6.0--72.0 | Markdown body text size in points. If `[markdown]` is omitted, it follows the global `font_size`; headings and other markdown styles scale relative to this value. |
| `margin_columns` | 2.0 | 0.0--24.0 | Left/right document margin measured in Markdown body character widths |

### Rendering

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `atlas_size` | 2048 | 512--4096 | Must be power of 2 |

### Scrolling

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `smooth_scroll` | true | | Trackpad momentum accumulation |
| `scroll_speed` | 1.0 | 0.1--10.0 | Multiplier; out-of-range logs WARN and resets to 1.0 |
| `scrollback_lines` | 10000 | 1--1000000 | Shell-host scrollback capacity; out-of-range logs WARN and resets to default |

### Notifications

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `enable_toast_notifications` | true | | Master switch for toast overlay |
| `toast_duration_s` | 4.0 | 0.5--60.0 | Seconds each toast remains on screen before fading |
| `chord_timeout_ms` | 1500 | `>= 100` | How long a chord prefix stays armed while waiting for the next key |
| `chord_indicator_fade_ms` | 2500 | `>= 100` | How long the top-bar chord indicator takes to fade after a chord completes or times out |

### Pane Status Bar

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `show_pane_status` | true | | One-cell-tall status strip below each pane showing host kind, dimensions, and (for shell hosts) cwd from OSC 7 |

### MegaCity (`[mega_city_code]` section)

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `code_source` | `treesitter_db` | `treesitter_db`, `graphify` | Selects whether MegaCity reads the semantic city directly from Tree-sitter scan output or directly from a Graphify graph JSON; `treesitter_db` remains the backward-compatible config token for the direct Tree-sitter source |
| `graphify_graph_path` | `graphify-out/graph.json` | | Graphify graph JSON path; relative paths resolve from the Draxul repository root |

### Terminal (`[terminal]` section)

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `fg` | `#eaeaea` | | Hex color (3 or 6 digit) |
| `bg` | `#141617` | | Hex color (3 or 6 digit) |
| `selection_max_cells` | 65536 | 256--1048576 | Maximum cells in a single selection before truncation |
| `copy_on_select` | true | | Auto-copy completed selections to the system clipboard |
| `paste_confirm_lines` | 5 | 0--100000 | Pastes with this many lines or more require `confirm_paste`. `0` disables |
| `url_detection` | true | | Detect HTTP/HTTPS URLs in grid text and make them clickable with Ctrl/Cmd-click |
| `enable_osc8_hyperlinks` | true | | Enable OSC 8 terminal hyperlink regions |

### Chrome (`[chrome]` section)

All values are hex colors in `#RRGGBB` or `#RGB` form. Omitted keys keep the built-in Catppuccin Mocha-inspired defaults.

| Key | Default | Notes |
|-----|---------|-------|
| `tab_bar_bg` | `#181825` | Top tab/status strip background |
| `tab_active_fg` | `#f5e0dc` | Active tab label text |
| `tab_inactive_fg` | `#cdd6f4` | Inactive tab label text |
| `tab_active_bg` | `#b93c3c` | Active tab number/accent fill |
| `tab_inactive_bg` | `#45475a` | Inactive tab and dim accent fill |
| `tab_editing_bg` | `#8c90af` | Tab rename field fill |
| `divider` | `#78788c` | Split divider line |
| `focus_border` | `#b93c3c` | Focused pane border |
| `status_bar_bg` | `#45475a` | Pane status pill body |
| `status_bar_fg` | `#cdd6f4` | Pane status text |
| `status_focused_accent_bg` | `#3ca55f` | Focused pane status number/accent fill |
| `status_inactive_accent_bg` | `#6e738c` | Unfocused pane status number/accent fill |
| `status_editing_bg` | `#8c90af` | Pane rename field fill |
| `resource_pill_bg` | `#f9e2af` | Normal CPU/RAM pill fill |
| `resource_pill_fg` | `#1a1a1f` | CPU/RAM pill text |
| `resource_pill_warn_bg` | `#f5c282` | CPU/RAM warning fill |
| `resource_pill_hot_bg` | `#f45656` | CPU/RAM hot fill |
| `chord_pill_bg` | `#45475a` | Active chord indicator fill |
| `weather_pill_bg` | `#474d61` | Weather pill fill |
| `editing_outline` | `#ffffff` | Rename caret and outline |

---

## CLI Flags

| Flag | Description |
|------|-------------|
| `--host <type>` | Host type: nvim, markdown, powershell, bash, zsh, wsl, megacity |
| `--command <cmd>` | Override host command path |
| `--source <path>` | Markdown file to view when launching `--host markdown`; MegaCity Tree-sitter scan root when launching `--host megacity` |
| `--session <id>` | Select which saved shell session to restore |
| `--persistent-app` | Opt into live detach/reattach: closing the window hides Draxul, and a later launch with this flag reattaches to the running instance |
| `--pick-session` | Open the session picker UI to browse, attach, restore, create, or kill shell sessions |
| `--new-session` | Start a fresh saved shell session; if `--session` is omitted Draxul generates a unique session id |
| `--session-name <name>` | Set the saved display name for the launched or restored shell session |
| `--rename-session` | Rename the selected running or saved shell session using `--session-name <name>` |
| `--list-sessions` | Print saved sessions with live/saved state plus workspace and pane counts |
| `--attach-session` | Explicitly activate a running persistent app session |
| `--detach-session` | Explicitly detach a running persistent app session without killing it |
| `--kill-session` | Explicitly kill a running persistent app session or delete its saved state |
| `--continuous-refresh` | Keep the MegaCity host rendering continuously and, on Vulkan, prefer unsynced presentation so frames do not wait for vblank |
| `--log-file <path>` | Write logs to file |
| `--log-level <level>` | Minimum level: error, warn, info, debug, trace |
| `--pty-capture-file <path>` | Capture raw terminal drain chunks to a replayable PTY log for terminal debugging |
| `--console` | (Windows) Allocate debug console window |
| `--smoke-test` | Non-interactive startup test, exits after 3s |
| `--render-test <file>` | Run render test scenario (requires DRAXUL_ENABLE_RENDER_TESTS) |
| `--bless-render-test` | Update reference image from test output |
| `--show-render-test-window` | Show window during render test |
| `--export-render-test <file>` | Export captured frame to BMP |

---

## Build

### Prerequisites
- CMake 3.25+
- Windows: Visual Studio 2022, Vulkan SDK (with glslc)
- macOS: Xcode Command Line Tools (Metal compiler)

### CMake Presets

| Preset | Platform | Description |
|--------|----------|-------------|
| `default` | Windows | Debug, VS 2022 x64 |
| `release` | Windows | Release |
| `win-ninja-debug` | Windows | Debug, Ninja Multi-Config local-iteration build in `build-ninja/` |
| `win-ninja-release` | Windows | Release, Ninja Multi-Config local-iteration build in `build-ninja/` |
| `mac-debug` | macOS | Debug |
| `mac-release` | macOS | Release |
| `mac-asan` | macOS | Debug + AddressSanitizer + UBSan |
| `mac-tsan` | macOS | Debug + ThreadSanitizer (mutually exclusive with ASan) |
| `mac-coverage` | macOS | Debug + LLVM coverage |
| `clang-tools` | macOS | Ninja, compile_commands.json only |

### Convenience Scripts

- `do run` configures, builds, and runs — defaults to Ninja on Windows, only builds the `draxul` target
- `do run relwithdebinfo` / `do build relwithdebinfo` use `RelWithDebInfo` on Windows for optimized builds with PDB symbols
- `do run --vs` falls back to the Visual Studio generator if you want the existing `build/` workflow
- `do run --ninja` forces the Ninja local-iteration path explicitly
- `do run release --host megacity --parser graphify` updates `[mega_city_code]` to use `code_source = "graphify"` with `graphify-out/graph.json`, strips the helper flag before launching, and starts MegaCity from that semantic source. `--parser treesitter` / `--parser treesitter_db` switches the same config back to the direct Tree-sitter semantic source
- `do review` / `do review-bugs` run Codex + Claude review passes by default, add Gemini on macOS, and use Codex for the final consensus pass
- `do consensus` / `do consensus-bugs` default to Codex; `claude`, `gemini`, and legacy `gpt` selector arguments are also accepted
- `do review-codex` runs just the Codex review helper; `do review-gpt` remains as a compatibility alias

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `DRAXUL_ENABLE_RENDER_TESTS` | ON | Render test/snapshot infrastructure |
| `DRAXUL_ENABLE_SANITIZERS` | OFF | ASan + UBSan |
| `DRAXUL_ENABLE_TSAN` | OFF | ThreadSanitizer (Clang/GCC only, mutually exclusive with `DRAXUL_ENABLE_SANITIZERS`) |
| `DRAXUL_ENABLE_COVERAGE` | OFF | LLVM source-based coverage |
| `DRAXUL_ENABLE_MEGACITY` | ON | MegaCity optional module (`modules/megacity/`) — when OFF, the terminal product builds with no megacity sources, headers, link dependency, or test coupling |
| `BUILD_TESTING` | ON | Test targets |

### Build Targets
- `draxul` -- Main executable (.app bundle on macOS)
- `draxul-tests` -- Unit test suite (Catch2)
- `draxul-rpc-fake` -- Fake RPC server for integration tests

### Dependencies (FetchContent, automatic)
SDL3, FreeType, HarfBuzz, MPack, ImGui, GLM, Catch2, vk-bootstrap (Windows), VMA (Windows)

### Compiler Cache
If `ccache` (or `sccache`) is found on `PATH`, the build automatically routes every C/C++ compile through it via `CMAKE_<LANG>_COMPILER_LAUNCHER`. The launcher is configured before `project()` so language-enablement compile probes also benefit. No effect when neither tool is installed.

### Shaders
- Windows: GLSL 4.50 -> SPIR-V via glslc
- macOS: Metal Shading Language -> metallib via xcrun

---

## CI (GitHub Actions)

| Workflow | Description |
|----------|-------------|
| `build.yml` | Manual-only (`workflow_dispatch`) Windows + macOS build/test pipeline; uploads config-matched app artifacts and render-test outputs. Accepts a `run_tsan` boolean input that optionally enables the non-gating `tsan-macos` job to run unit tests and the smoke test under ThreadSanitizer with `tsan.supp` suppressions |
| `asan.yml` | Manual-only (`workflow_dispatch`) AddressSanitizer builds (macOS) |
| `coverage.yml` | Manual-only (`workflow_dispatch`) LLVM coverage collection (macOS), uploads `build/coverage.lcov` as an artifact and to Codecov |
| `format.yml` | Manual-only (`workflow_dispatch`) clang-format lint |
| `sonar.yml` | Manual-only (`workflow_dispatch`) SonarCloud code quality |
| `docs.yml` | Manual-only (`workflow_dispatch`) documentation generation |

Both CI platforms install Neovim and run with `DRAXUL_RUN_SLOW_TESTS=1`.

---

## Render Test Infrastructure

- **Scenario files**: TOML in `tests/render/` with per-scenario font, size, DPI, commands
- **Reference images**: BMP files in `tests/render/reference/` (platform-suffixed)
- **Built-in scenarios**: basic-view, cmdline-view, unicode-view, ligatures-view, panel-view, wide-char-scroll
- **Comparison**: Pixel-diff with configurable tolerance and changed-pixel threshold
- **Blessing**: `py do.py blessbasic`, `blesscmdline`, `blessunicode`, `blessligatures`, `blessall`

---

## Logging

| Level | Macro | Notes |
|-------|-------|-------|
| Error | `DRAXUL_LOG_ERROR` | Always compiled |
| Warn | `DRAXUL_LOG_WARN` | Always compiled |
| Info | `DRAXUL_LOG_INFO` | Always compiled |
| Debug | `DRAXUL_LOG_DEBUG` | Stripped in release |
| Trace | `DRAXUL_LOG_TRACE` | Stripped in release |

Categories: App, Rpc, Nvim, Window, Font, Renderer, Input, Test.
Output: stderr (always) + optional file via `--log-file`.
