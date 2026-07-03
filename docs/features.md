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
| MegaCity | `--host megacity` | 3D demo host (semantic code city, textured road/sidewalk/tree materials, cascaded directional shadows, point-light cubemap shadows, screen-space AO, mouse-drag pan, Alt+drag orbit, direct Tree-sitter-to-semantic-snapshot scan, optional `--source` Tree-sitter scan-root override) |
| BioView | `--host bioview` | Experimental biological code visualization backed by the same neutral Tree-sitter semantic snapshot as MegaCity. Modules become tissue regions, source files become translucent ellipsoid cells, semantic symbols become nuclei/organelles, and current type/dependency references are drawn as fibres using the shared 3D scene renderer. |
| SatView | `--host satview` | Optional satellite-overview host with switchable interactive 3D globe, full-screen 2D equirectangular map, and Earth ground-observer sky views, Earth/Moon POV selection for globe/map views, date-aware day/night lighting, a real-scale ephemeris-driven Moon with an 8k NASA LRO texture and analytical orbit track, a ray-marched Rayleigh/Mie atmosphere, a Hipparcos tiny-quad starfield with persisted apparent-magnitude window and brightness controls, an elevated cloud shell using bundled clouds by default with an optional asynchronously cached near-real-time source, independently cached CelesTrak active-GP and SATCAT catalogs, precise SGP4 propagation plus clearly marked SATCAT summary estimates, population coloring/filtering for active payloads, inactive payloads, rocket bodies, debris, and unknown objects, all-sampled or selected-only path display, track/marker LOD controls, click and tree selection, an ImGui filter/details panel with a live simulation-clock readout in the user's local timezone, a `Real Time` action that restores the current system time at `1x`, smoothed quaternion left-drag orbit controls, Ctrl+drag and mouse-wheel dolly, MegaCity-style keyboard orbit/dolly controls, reset camera (`Home`), data refresh (`Ctrl+R`), panel toggle (`F1`), and time-speed controls (`Space`, `[`, `]`) |

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
- **MegaCity semantic snapshot**: The City Build UI builds the semantic city from the same neutral `CodeSemanticSnapshot` used by BioView. Tree-sitter scanner output is first projected into repository/module/file/type/function/method/field/reference nodes, then the city builder applies city-specific roles, building metrics, function layers, and dependency routing before layout. The old SQLite city snapshot module and Tree-sitter city adapter have been removed. Repository module boundaries are derived from paths, so `app/...`, `libs/<name>/...`, and `modules/<name>/...` appear as distinct city modules
- **BioView semantic presentation**: `--host bioview` reuses the MegaCity optional module's Tree-sitter scan and neutral `CodeSemanticSnapshot`, then builds an alternate biological scene without going through the city/building presentation model. The initial presentation draws module tissue regions, translucent ellipsoid file cells, type/function bodies, method/field/include organelles, and typed semantic-reference fibres through the existing cross-platform MegaCity render pass, so Vulkan and Metal stay aligned while the metaphor evolves. Its analysis UI exposes BioView-specific build controls and shared renderer controls rather than city/building, park, tree, sign, or road-layout sliders.
- **MegaCity performance preview and coverage modes**: The Codebase Analysis panel now exposes saved top-level `Perf`, `Coverage`, `LCOV Coverage`, and `Perf Log Scale` controls. `Perf` blends flat-color buildings toward a green-to-red heat palette per semantic building layer using smoothed live timing heat, while `Coverage` forces any touched/matched function layer to full heat so executed code lights up clearly. `LCOV Coverage` imports a static LLVM `lcov` tracefile from `db/coverage.lcov` or `build/coverage.lcov` and lights semantic function layers based on function-level test coverage from the LLVM coverage report — covered functions render as hot, uncovered stay at base color. The local `do.py coverage` flow exports `build/coverage.lcov` and refreshes `db/coverage.lcov` for app use. The debug panel shows LCOV-specific diagnostics (report functions, covered functions, matched/heated layers/buildings), and the building tooltip reports per-function coverage status. `Perf Log Scale` applies a visual logarithmic boost to low heat values so more active layers move toward the warm end without changing the underlying timing data. All modes are driven by a live or imported metrics snapshot for every building and function, indexed in the shader by stable building/layer ids, and accompanied by an in-panel matched/unmatched perf debug readout plus tooltip timing details for hovered functions
- **MegaCity sign sizing controls**: Building roof-sign rings can now enforce a configurable `Min Width / Char`, so long class/module labels can expand the repeated sign band instead of being squeezed into the default building footprint
- **MegaCity building shape thresholds**: The City Build UI now exposes both `Hex Threshold` and `Oct Threshold`, letting connected buildings step from 4-sided to 6-sided to 8-sided procedural shells based on total incident dependency count
- **MegaCity selection tuning**: Selection fade now has configurable dependency, hidden, hover-hidden, and road hidden alpha controls, with configurable spacebar-held raise/fall timing for hidden buildings so the shared road layer can remain fully visible while selected-context buildings read clearly
- **SatView Earth pass**: The optional SatView module renders a texture-mapped Earth through the shared 3D render-pass path on Vulkan and Metal, using staged 8k equirectangular day, night, and cloud maps with a UTC date-aware solar ephemeris, seasonal declination, Greenwich-sidereal-aligned TEME orbit tracks and satellite point markers, a quaternion-owned camera/manipulator with continuous pole-crossing orbit and dolly controls, a per-frame look-at view matrix, GPU marker billboards derived from the same camera quaternion, a generic binary Hipparcos star catalog rendered as additive instanced tiny quads behind globe and ground-sky views, and an orbit-aware dolly ceiling that expands to the visible satellite/orbit radius. Globe mode also renders the Moon at its analytical geocentric position, instantaneous Earth distance, and real radius using NASA's 8k LRO WAC color mosaic. The Moon has its own atmosphere-free diffuse pass, phase-dependent Earthshine, a stable IAU-pole-aligned tidal orientation, a visibility toggle, and an expanded camera ceiling that can frame the full Earth-Moon separation. Its independently toggleable pale orbit track spans one sidereal lunar cycle, uses the shared track-sample control, and remains stable until simulation time leaves the sampled half-month window; it is omitted from Moon map mode where a self-relative track has no useful projection. The same pass can switch to a six-vertex full-screen equirectangular map targeting either body and can enter an Earth ground-observer sky view by double-clicking the Earth globe or Earth map. Ground view places the camera just above the selected WGS-84 surface point, starts with a 60-degree angle of view and 0.1x marker scale, supports yaw/tilt look controls plus FOV and marker-scale adjustment, renders an optional full-screen Rayleigh/Mie ground-sky atmosphere pass from the surface viewpoint, and filters satellite markers and path segments to samples above the observer's local horizon. Earth POV projects markers to their current Earth subpoints and uses each orbit sample's own Earth-fixed position, producing temporal ground tracks rather than rotating an entire inertial orbit ring at one instant. Earth ground tracks remain open between their first and last sampled times, avoiding a false closing segment. Moon POV displays the lunar mosaic and projects every satellite and orbit point by its Moon-centered line of sight in the tidally locked lunar frame, so the Earth-orbiting catalog gathers around the Earth-facing hemisphere. Paired line endpoints and wrapped draw copies clip cleanly at both map edges without introducing internal path gaps. The map uses a rotated equirectangular projection whose center longitude/latitude can be changed without rebuilding or re-uploading the cached orbit buffer; surface sampling, solar lighting, markers, tracks, wrapping, and picking share the selected body's transform. Existing filters, colors, path modes, interpolation, selection, day/night lighting, and cloud source controls continue to apply. A separate atmosphere shell jointly ray-marches Rayleigh and Mie view/sunlight optical depth through 8 km molecular and 1.2 km aerosol scale heights, including wavelength-dependent extinction, Rayleigh and forward Henyey-Greenstein phase scattering, planet shadow, and premultiplied depth-aware composition. Clouds render on their own premultiplied shell about 9.5 km above the surface, giving them geometric separation and independent day/night lighting instead of painting them into the opaque Earth surface; in Earth map mode the selected cloud texture is composited directly over the flat Earth map. A background service downloads the latest 8k Live Cloud Maps EUMETSAT-derived texture and caches it for the source's three-hour update cadence. Bundled and live cloud maps keep separate GPU bindings: the bundled map is selected by default, `Realistic clouds` opts into the live map, and the master `Clouds` switch can disable the shell without discarding either texture. The controls show the advancing simulation date/time in the user's system timezone with its UTC offset. The `Real Time` button atomically unpauses, sets simulation time to the current system instant, resets speed to `1x`, and refreshes propagated positions. It is launched with `--host satview`.
- **SatView catalog service**: SatView parses and merges the CelesTrak `active` GP JSON feed with header-resolved RFC 4180 SATCAT CSV records by NORAD id. Active payloads retain their real mean elements; SATCAT supplies the broader split into active payloads, inactive payloads, rocket bodies, debris, and unknown cataloged objects plus owner, operational/data status, and radar-cross-section metadata. GP and SATCAT use independent last-good payload/metadata caches, two-hour and twelve-hour freshness guards respectively, and independent failure handling, so one unavailable source does not discard the other. Offline startup merges both caches when available and falls back to the bundled sample only when neither catalog is usable. Decayed, non-Earth, landed, impacted, and docked records are excluded; retained records with incomplete orbit summaries remain in population totals but are reported as non-renderable. This covers individually cataloged objects only, not small untracked fragments or dust.
- **SatView propagation service**: SatView keeps active GP records on the pinned Vallado/CelesTrak AIAA-2006-6753 SGP4 implementation. SATCAT-only records never enter SGP4: a separate deterministic summary solver reconstructs an ellipse from period/inclination/perigee/apogee, derives stable orientation and phase from the NORAD id, and labels every resulting marker and selected path `SATCAT summary estimate`. Satellite positions are divided into deterministic index ranges and propagated by a persistent worker pool; multi-satellite track generation uses the same pool, while small selected-only path jobs stay local to avoid dispatch overhead. GP-backed records stay first in the capped normal-track order, while selecting an estimated object still forces its path into the track set. Immutable names and catalog metadata are shared across dynamic snapshots so the larger merged population does not allocate strings every simulation tick. Completed states and tracks retain catalog order and solution fidelity through a non-blocking triple-buffer handoff; marker interpolation, synchronized Earth/map rendering, cached general tracks, selected-track resampling, and the optional `Refresh paths every step` control continue to apply. The shared Vulkan and Metal scene stream uses lower alpha for estimated markers and paths as an additional fidelity cue.
- **SatView interaction panel**: SatView owns a pane-local ImGui panel toggled by `F1`, with a `Globe`/`Map`/`Ground` view switch, an Earth/Moon POV selector for globe/map, ground latitude/longitude readout, ground angle-of-view and marker-scale sliders, pause/resume, reset camera/map center/ground look direction, reset-to-defaults, catalog/cloud refresh, time-speed, cloud visibility, realistic-cloud source selection, apparent-magnitude min/max star controls, star brightness scale, atmosphere and Moon visibility, and color modes for `Population` (the fresh-config default), `Name Prefix`, `Orbit Class`, or `Object Type`. Population counts and independent visibility controls cover active payloads, inactive payloads, rocket bodies, debris, and unknown objects; SATCAT summary estimates have a separate persistent visibility toggle. Search, orbit-class, object type/classification, per-object GP/SATCAT provenance, and element-age filters feed an object tree grouped as Population -> Orbit Class -> Name Prefix. The catalog area reports separate GP/SATCAT live/cache/failure state plus merged/renderable/skipped totals, rendered/available star counts, and notes both estimate fidelity and the absence of uncataloged debris. Selected details include population, solution label, owner/status, radar cross section, orbit class, period, available epoch age, WGS-84 position, altitude, and speed. Durable panel settings are merged into the `[satview]` user-config table and restored on startup, including projection mode, star magnitude window and brightness scale, ground angle of view, ground marker scale, and the last ground observer longitude/latitude; the reset action writes those settings back to their defaults and clears transient selection/camera/map/ground state. Existing track/marker LOD, path-display, camera, map, ground-view picking, and force-visible selection behavior remains available, with the track count defaulting to 10.
- **SatView sun-synchronous filter**: SatView derives a persistent `SSO candidates only` filter from each orbit's first-order J2 nodal-precession rate, using current GP elements when available and SATCAT period/inclination/perigee/apogee summaries otherwise. SSO remains an independent property layered on the existing LEO/MEO/GEO/HEO class, is shown in selected-object details, and intentionally includes debris or rocket bodies that still occupy a matching sun-synchronous plane.
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
- **SatView camera/map/ground**: In globe mode, left-drag orbits and `Ctrl` + left-drag/mouse wheel dollies; `A/D`, left/right arrows, or `Q/E` orbit horizontally, `W/S`, up/down arrows, or `T/G` orbit vertically, and `R/F` dolly in/out. In map mode, left-drag pans the projection while the same horizontal/vertical keys move its center longitude/latitude. Double-clicking the Earth globe or Earth map enters ground view; in ground view, left-drag and the same horizontal/vertical keys yaw and tilt the sky view, mouse wheel or `R/F` changes angle of view, and the panel provides Back to Globe/Map buttons.
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
- **Session-scoped shell restore CLI/UI**: `--session <id>` selects which saved shell session Draxul should restore, `--new-session` starts a fresh saved shell session (generating a unique id when `--session` is omitted), `--session-name <name>` sets the saved display name for a newly launched or restored session, `--rename-session --session-name <name>` renames a running or saved session, `--list-sessions` prints known sessions with live/detached/saved status and workspace/pane counts (preferring live owner summaries when available), `--persistent-app` enables live detach/reattach for desktop launches, `--attach-session` explicitly activates a running persistent app session, `--detach-session` explicitly detaches a running persistent app session without killing it, `--kill-session` explicitly kills a live session or deletes its saved topology, the command palette `save_session_as` action saves the current restorable shell topology under a prompted display name and switches the running app to the generated named session id, and the command palette `load_session` action shows a fuzzy selectable list of saved sessions and restores the selected saved topology in the current window.
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
| `save_session_as` | (unbound) |
| `load_session` | (unbound) |
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
| `--host <type>` | Host type: nvim, markdown, powershell, bash, zsh, wsl, megacity, bioview, satview |
| `--command <cmd>` | Override host command path |
| `--source <path>` | Markdown file to view when launching `--host markdown`; Tree-sitter scan root when launching `--host megacity` or `--host bioview` |
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
- `do run release --host megacity --parser treesitter` strips the helper flag before launching, writes `[mega_city_code].code_source = "treesitter_db"`, and removes stale `graphify_graph_path` entries from that section. `--parser treesitter_db` is accepted as the same helper alias
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
| `DRAXUL_ENABLE_SATVIEW` | ON | SatView optional module (`modules/satview/`) — when OFF, the terminal product builds with no SatView sources, headers, link dependency, or shader staging |
| `BUILD_TESTING` | ON | Test targets |

Markdown and Kanban are product modules under `modules/markdown/` and `modules/kanban/`. They are built by default and keep their existing host flags and CMake target names.

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
- Windows shader discovery uses CMake `CONFIGURE_DEPENDS`, so added `.vert`/`.frag` files trigger regeneration of the shader build rules during the next build
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
