# Draxul Features

Quick reference of all user-facing features, configuration, CLI flags, build options, and CI infrastructure.

---

## Host Types

| Host | Flag | Description |
|------|------|-------------|
| Neovim | `--host nvim` (default) | Embeds `nvim --embed` via msgpack-RPC over stdin/stdout pipes |
| Markdown | `--host markdown --source <file.md>` | Native Draxul markdown viewer host using the FreeType/HarfBuzz font pipeline, MD4C parsing, variable-height document rows, configurable body text size/margins, restrained styled headings, section indentation, front matter/code/list/table decorations, mouse wheel/PageUp/PageDown/Home/End plus Vim-style `j/k`, `Ctrl+F/B`, `gg`, `G` scrolling, and a draggable proportional scrollbar |
| Kanban | `--host kanban [--source <folder>]` | Native grid-backed kanban viewer for a `kanban/` folder. Subfolders become columns, Markdown files become cards, `.draxul-kanban.toml` stores ordering, Vim-style `h/j/k/l`, `Ctrl+F/B`, `gg`, and `G` move selection within the current column, shifted up/down arrows reorder cards, `<`/`>` move files between column folders, `z` zooms to the selected column full-width (`z` again restores all columns), `p` pins a bottom-third Markdown preview of the selected card that follows the selection (`p` again removes it), and Enter opens the selected card's Markdown file in a background Neovim host (reusing an existing Neovim pane or spawning a split) without moving focus off the board |
| Bash | `--host bash` | PTY-based terminal (Unix) |
| Zsh | `--host zsh` | PTY-based terminal (Unix) |
| PowerShell | `--host powershell` | ConPTY on Windows, PTY on macOS/Linux |
| WSL | `--host wsl` | Windows Subsystem for Linux shell |
| MegaCity | `--host megacity` | 3D demo host (semantic code city, textured road/sidewalk/tree materials, cascaded directional shadows, point-light cubemap shadows, screen-space AO, mouse-drag pan, Alt+drag orbit, direct Tree-sitter-to-semantic-snapshot scan, optional `--source` Tree-sitter scan-root override) |
| BioView | `--host bioview` | Experimental biological code visualization: the whole codebase grown as a living organism. Each **module** becomes a soft, colored **tissue territory**; each **class/struct** becomes a **cell** packed into its module's tissue; and strong **cross-module dependencies** become **blood vessels** arcing between tissues. The most significant classes render as full detailed cells (methods→mitochondria, fields→ribosomes, member roster→DNA rungs, inheritance→Golgi, dependencies→vesicles, oversized methods→lysosomes, organizer→centrosome), while the rest render as simpler module-tinted cells so hundreds of types stay affordable. All procedural geometry, lit by the shared 3D scene renderer; deterministic; every organelle carries a semantic ref back to its code node. |
| Score | `--host score [--source <file.musicxml/.mxl>]` | Music score viewer + adaptive learning runner ([docs/features/scoreview.md](features/scoreview.md)): Verovio-engraved MusicXML/`.mxl` piano scores; a paged reading view with piece-analysis overlays; conveyor and Roll (Guitar-Hero-style) practice modes with adaptive tempo, spelling-colored notes, guidance keyboard, waterfall, and per-bar tempo ladders; an adaptive practice composer with spaced review, drills, and overnight re-tests; metronome/audition/soundfont audio; dev-keyboard, MIDI, and microphone input; per-piece progress memory. Full narrative: [docs/features/scoreview.md](features/scoreview.md) |
| SatView | `--host satview` | Satellite-overview host: interactive 3D globe, full-screen 2D map, and ground-observer sky views; Sun/planet/major-moon POVs; CelesTrak GP + SATCAT catalogs with SGP4 propagation; HDR pipeline with stars, constellations, Milky Way, atmosphere, and clouds; surface-object catalogues; six ImGui dock panels with filters, selection, and time controls. Full narrative: [docs/features/satview.md](features/satview.md) |

Pane splits use the platform default shell (Zsh on macOS, PowerShell on Windows) regardless of primary host type.
Host names, aliases, platform support, test-only status, and split/new-tab visibility come from the registered provider metadata. Optional hosts that are not built are therefore absent from the command palette and rejected explicitly by `--host`; the hidden `nanovg-demo` provider remains directly launchable by the render harness.

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
- **SatView HDR surface pipeline**: SatView scene layers render into a linear `RGBA16F` target with MSAA fallback, ACES tone mapping, and persisted exposure/white-point controls; details in [docs/features/satview.md](features/satview.md#rendering-and-data-pipeline)
- **MegaCity module surfaces**: Each non-central module now draws a thin module-colored outline above the shared road layer so module footprints are readable beneath sidewalks and buildings
- **MegaCity park dressing**: Central park now includes a procedurally generated `DraxulTree` mesh with atlas-based PBR leaf cards
- **MegaCity dependency routing**: The City Map panel now overlays routed building-to-building dependency lines driven by Tree-sitter field references and road-only semantic routing, and the same routed polylines are emitted into the 3D scene as thin raised connection strips with a directional green-to-red gradient from source to target, plus a configurable per-route layer step for stacked overlap readability
- **MegaCity semantic filters**: The City Build UI can now hide test entities and struct-backed entities before layout/build
- **MegaCity stacked struct plates**: Same-footprint structs within a module are stacked vertically into compact square-section plate buildings with configurable gap, max-per-stack, and sign colors; each plate remains independently clickable with full dependency routing and per-plate tooltips
- **MegaCity building shading controls**: The City Build UI includes `Middle Strip Push`, `Alternate Darken`, `Flat Roughness`, and `Flat Metallic` controls for non-textured procedural buildings, so flat-color shells can get configurable per-level mid-band ripples, alternating-band darkening, roughness, and metallic without affecting roads, routes, signs, or other flat overlays
- **MegaCity projection toggle**: The renderer panel can switch the MegaCity camera between `Orthographic` and `Perspective`; the choice persists in config, keeps the existing orbit/pan/zoom interactions, and also drives perspective-aware cascade splits and screen-space zoom scaling
- **MegaCity semantic snapshot**: The City Build UI builds the semantic city from the same neutral `CodeSemanticSnapshot` used by BioView. Tree-sitter scanner output is first projected into repository/module/file/type/function/method/field/reference nodes, then the city builder applies city-specific roles, building metrics, function layers, and dependency routing before layout. The old SQLite city snapshot module and Tree-sitter city adapter have been removed. Repository module boundaries are derived from paths, so `app/...`, `libs/<name>/...`, and `modules/<name>/...` appear as distinct city modules
- **BioView procedural cell**: `--host bioview` grows a single, anatomically-suggestive eukaryotic cell entirely from procedural geometry, replacing the earlier flat ellipsoid-cell-and-fibre projection. The cell is wider and longer than it is tall and floats above the grid so it casts a soft shadow. A double-sided translucent membrane (a noise-displaced "blob" sphere) wraps a fainter cytosol shell; inside sits a nucleus with its own translucent violet envelope, a dense nucleolus, and a four-color DNA double helix (two swept-tube backbones plus alternating base-pair rungs). Warm bean-shaped mitochondria carry cristae ridges, a curved Golgi stack of bowed cisternae sits near the membrane, a folded rough endoplasmic reticulum of swept tubes is studded with bright ribosomes, and the cytoplasm is scattered with free ribosomes, golden mRNA strands, translucent vesicles, purple lysosomes, and a perpendicular centriole pair. All parts use per-vertex-colored flat-color PBR shading through the shared cross-platform MegaCity/BioView render pass (directional + point lights, cascaded shadows, SSAO, HDR tone mapping), so Vulkan and Metal stay aligned. Geometry is generated by the new `draxul-geometry` cell toolkit (`build_blob_mesh`, `build_dna_double_helix`, `build_mitochondrion`, `build_golgi`, `build_endoplasmic_reticulum`, `build_tube`, plus 3D value-noise and mesh transform/append helpers). Its analysis UI still exposes BioView-specific build controls and shared renderer controls rather than city/building, park, tree, sign, or road-layout sliders.
- **BioView semantic mapping**: the cell represents one **Type** (class/struct) from the Tree-sitter `CodeSemanticSnapshot` — deterministically the most significant one, `argmax(4·method_count + min(field_count,24) + 2·referenced_type_count)` with `line_count` then `qualified_name` tie-breaks (methods weighted high, field count capped so a giant plain-data config struct doesn't out-rank a real class). Its real members drive the organelles: each **method → a mitochondrion** (length from the method's line count, cristae ridges from how many distinct types it touches, warm→hot color from complexity, capped at 40 by line count); each **field → a ribosome** studded on the nuclear envelope (green-tinted if the field references another type); **every declared member → one DNA base-pair rung** in source-declaration order, four-color-coded by category (field, self-contained method, collaborator method, constructor/virtual, capped at 60); the **inheritance chain → a Golgi stack** (one cisterna per ancestor); distinct **outgoing type dependencies → vesicles**; **oversized methods (>60 lines) → purple lysosomes**; and the **constructor or busiest method → the centrosome**. Overall class health — average method length, coupling, and god-class size — tints the membrane (and DNA backbone) green→amber→red and drives membrane spikiness, so a bloated, highly-coupled class reads as an inflamed, crowded cell at a glance. Every organelle carries a `CodeVizSemanticRef` back to its semantic node (file, qualified name, node id) for future hover/pick identification. The build is fully deterministic (all placement seeded from stable hashes of member names); when the snapshot has no types it falls back to a generic decorative cell.
- **BioView tissue / organism**: `--host bioview` grows the *whole codebase* as one organism, not just a single cell. Every **module** becomes a soft, translucent, module-colored **tissue territory** (a flattened blob patch on the floor); every **class/struct** becomes a **cell** packed into its module's tissue via phyllotaxis (sunflower) placement, with the most significant classes clustered toward each tissue's center and sized by significance; and **strong cross-module dependency coupling** (aggregated `ReferencesType`/`Inherits` edges between two modules, threshold ≥3) becomes a crimson **blood vessel** tube arcing between the two tissues, its thickness scaling with the edge count. The top classes (default 10) render as full detailed organelle cells (the mapping above); all other classes render as cheaper module-tinted "simple" cells (membrane + small nucleus, health-shifted toward red) that share meshes so hundreds stay affordable. Total cells are capped (default 640, dropping the least significant with a logged count), vessels capped (default 20). Module tissues are shelf-packed on the floor and the organism is recentered at the origin; the camera frames the whole span and a key light is positioned for the full organism. Health for simple cells is derived cheaply from the type's own line count, coupling, and member count. Everything remains deterministic. Planned follow-ups: file-level sub-clustering boundaries, honest "fat cell" / "nerve" mappings for other code shapes, level-of-detail as you zoom, and per-organelle hover tooltips in bio mode.
- **MegaCity performance preview and coverage modes**: The Codebase Analysis panel now exposes saved top-level `Perf`, `Coverage`, `LCOV Coverage`, and `Perf Log Scale` controls. `Perf` blends flat-color buildings toward a green-to-red heat palette per semantic building layer using smoothed live timing heat, while `Coverage` forces any touched/matched function layer to full heat so executed code lights up clearly. `LCOV Coverage` imports a static LLVM `lcov` tracefile from `db/coverage.lcov` or `build/coverage.lcov` and lights semantic function layers based on function-level test coverage from the LLVM coverage report — covered functions render as hot, uncovered stay at base color. The local `do.py coverage` flow exports `build/coverage.lcov` and refreshes `db/coverage.lcov` for app use. The debug panel shows LCOV-specific diagnostics (report functions, covered functions, matched/heated layers/buildings), and the building tooltip reports per-function coverage status. `Perf Log Scale` applies a visual logarithmic boost to low heat values so more active layers move toward the warm end without changing the underlying timing data. All modes are driven by a live or imported metrics snapshot for every building and function, indexed in the shader by stable building/layer ids, and accompanied by an in-panel matched/unmatched perf debug readout plus tooltip timing details for hovered functions
- **MegaCity sign sizing controls**: Building roof-sign rings can now enforce a configurable `Min Width / Char`, so long class/module labels can expand the repeated sign band instead of being squeezed into the default building footprint
- **MegaCity building shape thresholds**: The City Build UI now exposes both `Hex Threshold` and `Oct Threshold`, letting connected buildings step from 4-sided to 6-sided to 8-sided procedural shells based on total incident dependency count
- **MegaCity selection tuning**: Selection fade now has configurable dependency, hidden, hover-hidden, and road hidden alpha controls, with configurable spacebar-held raise/fall timing for hidden buildings so the shared road layer can remain fully visible while selected-context buildings read clearly
- **SatView rendering and data pipeline**: the Earth/Moon/Sun/planet passes, surface-object and sky-orientation overlays, catalog/propagation services, sun-synchronous filter, and dock panels are documented in [docs/features/satview.md](features/satview.md#rendering-and-data-pipeline)
- **Native network transport**: Weather, SatView catalog, and live-cloud downloads use a shared bounded HTTP client backed by WinHTTP on Windows and `NSURLSession` on macOS. Requests have explicit connection and overall deadlines, per-service response-size limits, RFC 3986 query encoding, and cancellation before worker joins, so runtime networking no longer requires `curl` or passes URLs through a command shell. Weather responses are parsed as typed JSON and reject missing, non-finite, wrong-type, or out-of-range values.
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
- **Per-display DPI**: moving the window between displays with different scale factors re-initialises font metrics (SDL display-scale-changed events), so text stays sharp on mixed-DPI setups

---

## Terminal Emulation (shell hosts)

- **VT100+** escape sequence support (ANSI/256/24-bit SGR colors, cursor control, DECSTBM scroll regions, DECAWM auto-wrap `DECSET 7`, DECOM origin mode `DECSET 6`)
- **Scrollback**: Configurable row ring buffer with viewport offset (default 10000)
- **Alt screen**: Main/alt switching (`DECSET 1049`) with snapshot restore; if the window is resized while in alt-screen, the saved content is re-dimensioned before restore
- **Mouse modes**: None, button-click (`DECSET 1000`), drag (`DECSET 1002`), all-motion (`DECSET 1003`), SGR encoding (`DECSET 1006`)
- **xterm focus reporting**: DECSET `?1004` emits `CSI I` / `CSI O` on pane focus gain/loss
- **DEC special graphics / ACS**: `ESC ( 0`, `ESC ) 0`, `SO`, and `SI` map VT line-drawing characters to Unicode box-drawing glyphs
- **Bracketed paste**: VT-wrapped clipboard paste (`DECSET 2004`)
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
- **SatView camera/map/ground**: globe orbit/dolly, map panning, ground-view rotation, and the keyboard equivalents are documented in [docs/features/satview.md](features/satview.md#input-camera-map-and-ground-view)
- **Smooth scroll**: Trackpad momentum accumulation (configurable speed multiplier)
- **File drop**: Native drag-and-drop dispatched to host as `open_file:` action
- **Kanban navigation**: Kanban panes support Vim-style card selection with `h/j/k/l`, `Ctrl+F/B` page jumps, `gg`/`G` beginning/end jumps within the current column, shifted up/down arrows for reordering cards, `<`/`>` for moving files between columns, `r` reload, and Enter to open the selected card's Markdown file for editing in a Neovim host.
- **Kanban column zoom**: `z` collapses the board to just the selected column at full width (moving left/right pages between columns while zoomed); `z` again restores the multi-column view.
- **Kanban card preview**: `p` pins a live Markdown preview pane across the bottom third of the board that always renders the currently selected card; it follows the selection as you move and Enter keeps input focus on the board so the preview and the board stay in view together. `p` again closes the preview.
- **GUI keybindings**: Chord-style prefix bindings (e.g. `ctrl+s, |`)
- **Command palette**: `Ctrl+Shift+P` opens a centered fuzzy-search overlay for all GUI actions with fzf-style scoring, `Ctrl+J/K` navigation, keybinding hints, and palette-rendered text prompts for actions needing short values
- **Print pane** (`print_pane` action, palette or `[keybindings]`): captures the focused pane's pixels, composes a single-page A4 PDF (aspect-fit inside margins, auto landscape for wide panes, CoreGraphics), and presents the native macOS print dialog for it (PDFKit print operation: preview, printer/paper choice, and auto-rotation so landscape pages land correctly on portrait paper); toasts report printed/canceled/failed. Hosts advise the printer via `IHost::print_hint()` — a pane-relative content rect plus a paper-white flag — so ScoreView prints just the page/band (no backdrop border) with its warm screen sheet tint snapped to pure white instead of printed stipple. macOS-only for now. `DRAXUL_PRINT_DRY_RUN=1` composes the PDF but skips the dialog and toasts the temp path (test hook)
- **`--gui-action <name>` CLI test hook**: with `--screenshot`, pumps until content is ready, dispatches any canonical GUI action by name, then captures — lets headless runs exercise palette actions and verify their toasts/effects
- **Config reload**: `reload_config` rereads `config.toml` on demand so palette alpha, keybindings, scroll settings, ligatures, terminal font changes, and Markdown font/margin changes can be applied without a restart

---

## Split Panes

- Binary split tree with vertical and horizontal splits
- Invisible four-pixel split gutters with ratio-based sizing — hovering a gutter switches the mouse cursor to the platform EW/NS resize cursor; click-and-drag updates the ratio in real time without drawing a divider line
- Per-pane host instance with independent lifecycle
- Focus tracking and pane-aware input routing
- Each pane leaves a four-pixel margin before its full rectangular focus frame. Window-facing edges add another two pixels while pane-to-pane edges stay unchanged, balancing the doubled margins at split joins without widening those joins. The host viewport follows the same edge-aware insets, keeping the configured red active frame (or subtle grey inactive frame) clear of both the pane edge and its content.
- Pane status uses one cell-high pill band. Any fractional terminal-row tail is painted with the host background, so it remains visually part of the content instead of making the status band look oversized.
- Keyboard-driven pane focus navigation (`Ctrl+H/J/K/L` vim-style) via `focus_left`, `focus_right`, `focus_up`, `focus_down` actions
- Keyboard-driven pane resizing via `resize_pane_left`, `resize_pane_right`, `resize_pane_up`, `resize_pane_down` actions (each nudges the nearest enclosing divider by 5%)
- **Pane zoom**: `toggle_zoom` action (default `Ctrl+S, z`) expands the focused pane to fill the full window; toggling again restores the previous split layout exactly (like tmux `Ctrl+B z`)
- **Close pane**: Closes the focused pane and its host; if last pane, exits the app
- **Shell session restore from saved topology**: Normal desktop launches periodically checkpoint shell-session tabs, split layout, focus, pane names, tab names, launch commands, and working directories in a local session-state file, save again on clean shutdown, and restore that topology by respawning panes on the next launch. Closing the final window exits Draxul; no hidden background owner remains. This is still shell-host only and not full crash recovery yet.
- **Session-scoped shell restore CLI/UI**: `--session <id>` selects which saved shell session Draxul should restore, `--new-session` starts a fresh saved shell session (generating a unique id when `--session` is omitted), `--session-name <name>` sets its display name, `--rename-session --session-name <name>` renames a saved session, `--list-sessions` prints saved sessions with Space/tab/pane counts, and `--delete-session` deletes saved topology. In the running app, `save_session_as` saves the current topology under a prompted name and switches to the generated session id; `load_session` shows a fuzzy list of saved sessions and restores the selection in the current window.
- **Abnormally exited shell panes stay inspectable**: If a shell pane dies unexpectedly, Draxul keeps the pane and its last rendered output visible instead of immediately tearing it down. The pane status pill shows `[exited]`, a toast points you at `restart_host`, and the existing restart action respawns the host in place. Clean shell exits still close the pane normally.
- **Session startup messaging**: Shell sessions surface a toast when Draxul starts a brand-new session or restores saved topology, so the user can tell which path was taken.
- **Restart host**: Kills the current host in the focused pane and relaunches with the same arguments
- **Swap pane**: Swaps the focused pane with the next pane in spatial order

---

## Spaces

- The live hierarchy is **Session -> Space -> Tab -> Pane**. A Space is a local project/task container with its own tabs, split layouts, hosts, and default root directory.
- A Draxul process can own multiple live Spaces. Switching the active Space preserves every inactive Space's panes and processes; inactive hosts continue to be pumped.
- The left rail appears once a second Space exists. Its upper Spaces section uses the shared segmented pill component (`1: Name`): a palette-blue number accent for the selected Space (grey when inactive), followed by the name on the standard grey pill body. Click a pill to activate it. A horizontal application-shell divider follows the Space list and reserves a lower Agents section; this first slice renders the `AGENTS` heading while agent rows are still future work. The rail background uses the same dark-grey chrome colour as the surrounding UI and default console background. Drag the rail's right-hand divider to resize it; the width snaps to terminal columns and is retained across launches.
- `new_space`, `switch_space`, `rename_space`, and `close_space` are available in the command palette. They are unbound by default.
- A new Space inherits the focused host's current working directory when possible. Its root directory becomes the fallback working directory for new hosts in that Space.
- Closing a Space terminates the hosts it owns. The final Space cannot be closed.
- Spaces are currently local. Session snapshots use a version-2 Space envelope, transparently migrate version-1 files in memory, and atomically checkpoint the complete ordered Space collection, including inactive Spaces. Restoring a version-2 multi-Space collection is the next persistence stage; suspend/resume, background ownership, SSH, and remote Spaces remain future work.

---

## Tabs

- Multiple tabs, each with its own independent split tree and host set
- Space, tab, and pane-status labels share one pill layout and palette model for capsule size, number accent width, text columns, foreground contrast, and active/inactive/editing colours.
- The top tab bar remains visible even with a single tab and shows right-aligned pills for live system usage and active chord prefixes
- `new_tab` (`Ctrl+S, C`): Create a new tab
- `close_tab` (`Ctrl+S, &`): Close the active tab (disabled when only one tab remains)
- `next_tab` (`Ctrl+S, N`): Cycle to the next tab
- `prev_tab` (`Ctrl+S, P`): Cycle to the previous tab
- Tab switching preserves focus state per tab (focus lost/gained notifications)
- **Inline tab rename**: double-click a tab pill (or press `Ctrl+S, ,` — tmux-style chord) to edit the tab name in place. Enter commits, Escape cancels, Backspace/Delete/Home/End/Left/Right work as expected. Empty commits leave the existing name untouched.
- **OSC 7 default naming**: shell hosts (e.g. zsh) drive the tab name from the OSC 7 working-directory escape until the user explicitly renames the tab; once the user sets a name, OSC 7 updates no longer overwrite it.
- **Inline pane rename**: double-click a pane status pill (or press `Ctrl+S, .`) to set a per-pane override name. Empty commit clears the override and reverts to the host-provided status text. Pane name overrides are in-memory only and follow the leaf for the lifetime of the session.
- **Luminance-based pill text colour**: tab and pane pill text colour is chosen automatically from the underlying NanoVG fill via BT.709 relative luminance, so any future background tweak gets a readable foreground without re-tuning a constant.

---

## Diagnostics Panel (ImGui)

Toggle with F12. Shows:

- Display DPI, cell size, grid dimensions, dirty cell count
- Frame timing (current + average)
- Atlas usage ratio and glyph count
- Startup profiling step timings
- MegaCity renderer controls, including module filtering (`All Modules` or a selected module), a `Point Shadow Debug Scene` toggle, debug views (`Final Scene`, `Ambient Occlusion`, `Normals`, `World Position`, `Roughness`, `Metallic`, `Albedo`, `Tangents`, `UV`, `Depth`, `Bitangents`, `TBN Packed`, `Directional Shadow`, `Point Shadow`, `Point Shadow Face`, `Point Shadow Stored Depth`, `Point Shadow Depth Delta`), tone-mapping controls, AO tuning, shadow-map inspection, and configurable connected-building hex/oct thresholds
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
| `quit` | `Ctrl + S, Q` |
| `save_session_as` | (unbound) |
| `load_session` | (unbound) |
| `new_space` | (unbound) |
| `switch_space` | (unbound) |
| `rename_space` | (unbound) |
| `close_space` | (unbound) |
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

Key syntax: modifiers `Ctrl`/`Control`, `Shift`, `Alt`, `Super`/`Meta`/`Gui` (case-insensitive), combined with `+` (e.g. `"Ctrl+Shift+V"`). Symbol aliases: `=`/`equals`, `-`/`minus`, `+`/`plus`, `|`/`pipe`. Any other key uses its SDL key name (`F1`--`F12`, `Tab`, `Return`, `Escape`, `Space`, `Home`, `End`, `PageUp`, `PageDown`, arrow keys, ...).

---

## Configuration (config.toml)

Draxul reads `config.toml` on startup and creates it with defaults on first save if it does not exist.

| Platform | Path |
|----------|------|
| Windows  | `%APPDATA%\draxul\config.toml` |
| macOS    | `~/Library/Application Support/draxul/config.toml` |
| Linux    | `$XDG_CONFIG_HOME/draxul/config.toml` (falls back to `~/.config/draxul/config.toml`) |

### Display

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `window_width` | 1280 | 800--8000 | |
| `window_height` | 800 | 600--8000 | |

### Font

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `font_size` | 11.0 | 6.0--72.0 | Points; 0.5pt step on increase/decrease |
| `space_sidebar_columns` | 20 | 12--48 | Preferred width of the multi-Space navigation rail in terminal columns |
| `font_path` | (bundled) | | Primary font file path |
| `bold_font_path` | (none) | | Bold variant |
| `italic_font_path` | (none) | | Italic variant |
| `bold_italic_font_path` | (none) | | Bold + italic variant |
| `fallback_paths` | [] | | Array of fallback font paths |
| `enable_ligatures` | true | | Programming ligature combining |

### GUI

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `palette_bg_alpha` | 0.9 | 0.0--1.0 | Command palette background opacity; clamped |
| `focus_border_width` | 3.0 | 1.0--10.0 | Focused-pane border thickness in pixels; clamped |
| `weather_location` | (empty) | | Weather pill: a city name (`"York, UK"`) or `lat,lon` (`"53.96,-1.08"`) shows the current temperature in the top-right chrome bar; empty disables |

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
| `code_source` | `treesitter_db` | `treesitter_db` | Legacy source selector; stale values such as `graphify` load as the direct Tree-sitter source and are rewritten as `treesitter_db` when MegaCity saves config |

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
| `enable_shell_integration_marks` | true | | Track OSC 133 shell-integration marks (prompt/command/output boundaries with exit codes) emitted by supporting shells |

### Chrome (`[chrome]` section)

All values are hex colors in `#RRGGBB` or `#RGB` form. Omitted keys keep the built-in Catppuccin Mocha-inspired defaults.

| Key | Default | Notes |
|-----|---------|-------|
| `tab_bar_bg` | `#161616` | Application chrome background: tab bar, Spaces rail, and pane gutters |
| `tab_active_fg` | `#f5e0dc` | Active tab label text |
| `tab_inactive_fg` | `#cdd6f4` | Inactive tab label text |
| `space_active_bg` | `#89b4fa` | Active Space number/accent fill |
| `tab_active_bg` | `#b93c3c` | Active tab number/accent fill |
| `tab_inactive_bg` | `#45475a` | Inactive tab and dim accent fill |
| `tab_editing_bg` | `#8c90af` | Tab rename field fill |
| `divider` | `#78788c` | Spaces/Agents section divider |
| `focus_border` | `#7b2828` | Focused border when a tab has multiple visible panes |
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
| `--host <type>` | Host type: nvim, markdown, powershell, bash, zsh, wsl, megacity, bioview, satview, score |
| `--command <cmd>` | Override host command path |
| `--source <path>` | Markdown file for `--host markdown`; Tree-sitter scan root for `--host megacity` or `--host bioview`; MusicXML or `.mxl` score for `--host score` |
| `--session <id>` | Select which saved shell session to restore |
| `--new-session` | Start a fresh saved shell session; if `--session` is omitted Draxul generates a unique session id |
| `--session-name <name>` | Set the saved display name for the launched or restored shell session |
| `--rename-session` | Rename the selected saved shell session using `--session-name <name>` |
| `--list-sessions` | Print saved sessions with Space, tab, and pane counts |
| `--delete-session` | Delete the selected saved shell session |
| `--continuous-refresh` | Let animation/3D hosts request frames continuously; use `--no-vblank` separately when unsynced presentation is desired |
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
| `win-ninja-debug` | Windows | Debug, Ninja Multi-Config local-iteration build in `build-ninja-debug/` |
| `win-ninja-release` | Windows | Release, Ninja Multi-Config local-iteration build in `build-ninja-release/` |
| `win-ninja-relwithdebinfo` | Windows | RelWithDebInfo, Ninja Multi-Config local-iteration build in `build-ninja-relwithdebinfo/` |
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
- `do test` configures Debug as needed, builds only `draxul-tests` and its helper/dependency targets, and runs the unit suite as four parallel Catch2 shards plus the Python `tests/do_py_tests.py` suite. It does not build or launch the app and does not run smoke or render snapshots
- `do clean` recursively removes repository-root build directories named `build/` or `build-*`, covering Visual Studio, Ninja, tooling, and custom build trees. It succeeds when none exist and preserves deploy packages, render outputs and references, databases, source files, and similarly named regular files
- `do hygiene` fails (exit 1) if a forbidden artifact is tracked — OS/coverage temps (`.DS_Store`, partial-transfer `.!*`, `*.profraw`, `*.profdata`) anywhere, or `key.txt` / `NUL.obj` / `megacity-linux-drivers-mesh.bmp` / stray `*.log`, `*.obj`, `*.bmp` at the repo root — or if the feature docs have duplicated (`docs/features.md` must exist and root `FEATURES.md` must stay a short pointer, not a second inventory). Legitimate nested assets (mesh `*.obj`, render-reference `*.bmp`) are allowed
- `do kanban-report` reads `kanban/` as the authoritative tracker and prints lane counts, flags `kanban/done` cards that still carry unchecked task boxes, and lists fully-ticked `kanban/pending` cards as move candidates. It is strictly read-only — it never edits, ticks, or moves a card
- Normal Debug/Release presets explicitly disable coverage and sanitizers, and the test scripts reject an instrumented shared cache before running. This prevents a prior coverage/ASan/TSan configure from silently slowing or changing the ordinary unit workflow
- `do smoke` remains the explicit startup check; the individual render shortcuts and `renderall` remain the explicit visual checks. `t.sh`, `t.bat`, and `scripts/run_tests.*` retain the full unit + smoke + available render-snapshot workflow for pre-commit, release, and CI validation
- `do run release --host megacity --parser treesitter` strips the helper flag before launching, writes `[mega_city_code].code_source = "treesitter_db"`, and removes stale `graphify_graph_path` entries from that section. `--parser treesitter_db` is accepted as the same helper alias
- `do deploy` creates a Release build, stages the runtime payload into `deploy/YYYY_MM_DD/mac` or `deploy/YYYY_MM_DD/win`, and writes a matching `draxul-YYYY_MM_DD-mac|win.zip` archive under the date folder. Windows packages contain only `draxul.exe`, its Microsoft C++ and adjacent runtime DLLs, compiled shaders, bundled fonts, and runtime assets; CMake metadata, object files, static libraries, tests, and source/build directories are excluded
- `do review features`, `do review bugs`, and `do review refactor` run focused Codex, Gemini/Agy, and Claude review passes followed by the matching Codex consensus; plain `do review` defaults to `features`, while `do review-bugs` remains a compatibility alias
- `do consensus features|bugs|refactor` reruns only the matching Codex consensus over the latest focused reviews; plain `do consensus` defaults to `features`, while `do consensus-bugs` remains a compatibility alias
- A reviewer selector can follow the review kind (`codex`, `agy`/`gemini`, or `claude`) to run one review without consensus; `do review-codex` and the legacy `do review-gpt` shortcut run the feature review unless another kind is supplied

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `DRAXUL_ENABLE_RENDER_TESTS` | ON | Render test/snapshot infrastructure |
| `DRAXUL_ENABLE_SANITIZERS` | OFF | ASan + UBSan |
| `DRAXUL_ENABLE_TSAN` | OFF | ThreadSanitizer (Clang/GCC only, mutually exclusive with `DRAXUL_ENABLE_SANITIZERS`) |
| `DRAXUL_ENABLE_COVERAGE` | OFF | LLVM source-based coverage |
| `DRAXUL_ENABLE_MEGACITY` | ON | MegaCity optional module (`modules/megacity/`) — when OFF, the terminal product builds with no megacity sources, headers, link dependency, or test coupling |
| `DRAXUL_ENABLE_SATVIEW` | ON | SatView optional module (`modules/satview/`) — when OFF, the terminal product builds with no SatView sources, headers, link dependency, or shader staging |
| `DRAXUL_ENABLE_SCOREVIEW` | ON on Windows/macOS | ScoreView optional module (`modules/score/`) with the pinned Verovio engraving runtime |
| `BUILD_TESTING` | ON | Test targets |

Markdown and Kanban are product modules under `modules/markdown/` and `modules/kanban/`. They are built by default and keep their existing host flags and CMake target names.

### Build Targets
- `draxul` -- Main executable (.app bundle on macOS)
- `draxul-tests` -- Unit test suite (Catch2), compiled with a test-only precompiled header and registered as four disjoint CTest shards labeled `unit`
- `draxul-rpc-fake` -- Fake RPC server for integration tests

ScoreView builds as five libraries inside the `DRAXUL_ENABLE_SCOREVIEW` gate — `draxul-score-learn`, `draxul-score-input`, `draxul-score-audio`, `draxul-scoreview`, `draxul-scoreview-host`; the per-library layering and dependency-isolation rationale is documented in [docs/features/scoreview.md](features/scoreview.md#build-structure).

CTest also registers `tests/do_py_tests.py` under the `unit` label. App smoke and render-snapshot tests use a shared CTest resource lock so full parallel test runs never overlap GPU/application processes.

### Dependencies (FetchContent, automatic)
SDL3, FreeType, HarfBuzz, MPack, ImGui, GLM, Catch2, vk-bootstrap (Windows), VMA (Windows)

### Compiler Cache
If `ccache` (or `sccache`) is found on `PATH`, the build automatically routes every C/C++ compile through it via `CMAKE_<LANG>_COMPILER_LAUNCHER`. The launcher is configured before `project()` so language-enablement compile probes also benefit. No effect when neither tool is installed.

### Shaders
- Windows: GLSL 4.50 -> SPIR-V via glslc
- Windows shader discovery uses CMake `CONFIGURE_DEPENDS`, so added `.vert`/`.frag` files trigger regeneration of the shader build rules during the next build
- macOS: Metal Shading Language -> metallib via xcrun

---

## CI (GitHub Actions)

| Workflow | Description |
|----------|-------------|
| `build.yml` | Windows + macOS build/test pipeline, run automatically for pushes and pull requests to `main` or manually through `workflow_dispatch`; uploads the Windows app artifact and both platforms' render-test outputs |

Both CI platforms install Neovim and run with `DRAXUL_RUN_SLOW_TESTS=1`.
Sanitizer and coverage presets remain available for local diagnostics but are not separate GitHub Actions workflows.

---

## Render Test Infrastructure

- **Scenario inventory**: `tests/render/manifest.json` is the single source for CTest registration, `do.py` commands, required platform references, and regression/developer/documentation status
- **Scenario files**: TOML in `tests/render/` with per-scenario font, size, DPI, commands; undeclared or missing files fail validation
- **Reference images**: BMP files in `tests/render/reference/` (platform-suffixed)
- **Regression scenarios**: basic-view, cmdline-view, unicode-view, panel-view, nanovg-demo
- **Developer-only scenario**: wide-char-scroll (not in CTest until both platform references exist); README and Claude-logo scenarios are documentation-only
- **Comparison**: Pixel-diff with configurable tolerance and changed-pixel threshold
- **Blessing**: scenario commands and `py do.py blessall` are derived from the manifest

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
