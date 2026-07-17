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
| BioView | `--host bioview` | Experimental biological code visualization: the whole codebase grown as a living organism. Each **module** becomes a soft, colored **tissue territory**; each **class/struct** becomes a **cell** packed into its module's tissue; and strong **cross-module dependencies** become **blood vessels** arcing between tissues. The most significant classes render as full detailed cells (methods→mitochondria, fields→ribosomes, member roster→DNA rungs, inheritance→Golgi, dependencies→vesicles, oversized methods→lysosomes, organizer→centrosome), while the rest render as simpler module-tinted cells so hundreds of types stay affordable. All procedural geometry, lit by the shared 3D scene renderer; deterministic; every organelle carries a semantic ref back to its code node. |
| Score | `--host score [--source <file.musicxml/.mxl>]` | Music score viewer + learning-runner host ([plans/scoreview.md](../plans/scoreview.md), manifesto in [plans/scoreview-manifesto.md](../plans/scoreview-manifesto.md)). **Opens straight into the runner**: with a `--source`, startup is the conveyor in Roll mode with the dev keyboard, transport rolling from the first note (`--command paged` gives the reading view instead; `f` toggles between them any time — the reading view is always the whole piece, reloading the full source even when the runner is on a rolling window, and toggling back re-arms the runner). Renders MusicXML (or compressed `.mxl`) piano scores engraved by an embedded, pinned Verovio 6.2.1 layout engine: pages lay out to fit the window width, Verovio's SVG dialect is interpreted into a resolution-independent draw list (SMuFL glyph outlines instanced by affine transform; staff lines/stems/beams/slurs/ties/hairpins as exact paths; serif text runs with Verovio's class styling), and replayed each frame through the shared NanoVG pass — crisp at any DPI, with white page sheets and drop shadows on a neutral backdrop. Vertical scrolling (wheel/trackpad, `j`/`k`, arrows, PageUp/PageDown/Space, Home/End), zoom through the shared `font_increase`/`font_decrease`/`font_reset` actions, page and zoom readout plus title/composer (from the draxul-notation semantic importer) in the status bar, and a placeholder engraved grand-staff page when launched without `--source`. **Conveyor (flow) mode** (`f` toggles): the whole piece re-engraves as one endless system on a full-width band, driven by a transport clock on a quarter-note axis — `Space` play/pause, `[`/`]` tempo (clamped 25%–120% of the piece's marking, starting at 60%), `r` rewind; a playhead that fills in from the left, settles ~30% across (never past the history the rolling window supplies, so it doesn't snap on a window advance), then holds while the music scrolls under it via Verovio's timemap, and every note lights up in amber as the playhead crosses its onset (per-op element-id highlight overlay). Grace-note clusters fold into their beat for the playhead and the gates: Verovio timestamps an acciaccatura a sliver (~1/16 quarter) from its principal while engraving them a wide gap apart, which made the playhead leap ~half a bar in ~30ms at every ornament — folded, the cluster anchors at its beat column, moves smoothly, and its notes judge together (the sliver is far inside the ±0.45-beat hit window). A **Proportional spacing** inspector checkbox (experimental, default off) re-engraves the flow strip with note space linear in duration (Verovio `spacingNonLinear` 1.0 instead of the engraver's 0.6 curve) at a compressed width multiplier (`spacingLinear` 0.06 vs Verovio's 0.25 — strictly proportional widths made one bar fill the screen; the tuned point keeps bars ~1.7x authentic width, ~8 on screen): typical scroll breathing (p90/p10) tightens from ~1.60 to ~1.25 and the worst segment from ~1.82x to ~1.60x median, with score columns tracking the time-proportional waterfall — glyph minimums (accidentals, clef changes) put a floor under further compression. Toggling re-engraves in place, keeping position and verdicts; the `f` paged reading view always keeps authentic engraving spacing. A **Spacing debug** inspector section exposes both knobs directly — `spacingLinear` (log slider) and `spacingNonLinear` — applied on slider release (each apply is a full re-engrave), overriding the preset until its **Reset to preset** button (switching the preset checkbox also clears overrides). Status shows play state, tempo qpm and % of marking. `--command flow`/`flow-autoplay` starts in flow mode (test/dev hook). **Roll mode — the runner** ([plans/scoreview-runner.md](../plans/scoreview-runner.md); the default game, also `g` inside the conveyor, `Esc` exits): Guitar Hero for piano — the transport never waits; each note judges in a ±0.45-beat hit window around its playhead crossing (right pitch inside the window = a hit; the window closing unplayed = a miss, flagged on the sheet with a small cross over the note; stray pitches count as wrong notes, tracked separately as the practice generator's raw material). Tempo holds and eases from demonstrated accuracy — a per-note EMA steps it up ~1%/onset toward the marking band's cap while accurate, down faster while struggling, clamped 25%–120% of the marking from a 60% start. An inspector **Lock tempo** checkbox (or `locktempo` token) freezes this — the piece plays at its proper marking with no adaptation. Chords judge per pitch, tie continuations auto-satisfy and re-voicing them is free. Player input flows through the `IPlayerInput` seam (dev keyboard: `z`-row chromatic octave anchored to the pending notes, `Return` = play what's due perfectly, `Backspace` = fluff exactly one note — a randomly chosen required pitch replaced by an adjacent wrong note while the rest play correctly). Status pill shows `>`, tempo, accuracy %, score, streak, misses, and wrong-note count. **Player memory** ([plans/scoreview-stream.md](../plans/scoreview-stream.md) S0): every Roll hit records its signed timing delta (beats, negative = early) and a center-weighted quality that feeds the accuracy EMA — sloppy hits stay green but slow the tempo; misses, near-miss strays, and chord clean/split/miss outcomes aggregate into a per-piece **progress JSON** (keyed by source content hash, under the draxul config dir at `scoreview/progress/`, versioned schema, unknown fields preserved, atomic tmp+rename writes, flushed per bar and at session end). Sessions resume at the stored tempo, and per-onset recent-encounter rings make bar mastery a consistency measure, not a lifetime average — the raw material for the practice generator. Per-bar right/wrong tallies (and per-hand within each bar — notes below middle C count as the left hand, at or above as the right, a deliberately fuzzy split) are tracked and persisted too, surfaced as a tree in the inspector. **Rolling window** (S2): Roll mode plays on a small re-engraved window of the piece instead of the monolithic strip — a `SourceSlicer` re-emits bar ranges as self-contained MusicXML (attribute state injected at the window head — gap-fill only, so a head measure that itself re-declares a clef or key keeps its own declaration; Verovio resolves same-position duplicates first-wins, which once lost the Grieg's measure-53 bass-clef return and drew the left hand on ledger lines below a treble staff — engrave-equivalence unit-proven against the monolith), the window advances only when the playhead nears its tail (the look-ahead running low), NOT every bar. Each advance is engraved on a **background thread with its own Verovio toolkit** (async double-buffer): the ~100 ms load-and-extract runs off the main thread while the current window keeps playing, and the finished window swaps into place on the main thread in well under a millisecond — so a window advance no longer drops a frame (the earlier synchronous rebuild froze ~7 frames and, at a fast/locked tempo, stuttered). A generous 14-bar look-ahead gives the background engrave ample runway; the initial window at game start is still a one-time synchronous engrave, and if the worker can't start the stream falls back to synchronous rebuilds. Carry APIs move tempo/score/accuracy across each rebuild while earned verdict colors repaint from an archive. `r` restarts the stream from the top but keeps the tempo the player has settled at (their learned pace — a restart is not a reason to jump back to the 60% start; the tempo lock/marking still wins). **The composer** (S3): Roll mode's stream is a PROGRAM, not just the piece — a `StreamComposer` plans each bar slot from the live player model: piece bars at a walking frontier, spaced review slices of weak bars (the "sliced up" spaced repetition), and fabricated chord-drill bars built from the exact voiced pitches in trouble (repeated grab over a held bass, written as MusicXML in the piece's meter, engraved by Verovio through the same window path — unit-proven onset-exact). S4's simplification ladder: the worst deeply-struggling bar returns with its weak hand ALONE (the real measure, other staff surgically removed), chord drills start BROKEN (arpeggiated onto the block grab) and climb as they land, troubled registers earn in-key scale fragments, and trouble is net of clean grabs so drills retire themselves; the special chain rotates so no one trouble type monopolizes. **Every note on the sheet is colored by its SPELLING** from a 21-color pairing palette (indexed by diatonic letter and accidental in CIELAB). The seven naturals — the white keys — use the **Boomwhackers** colors learners already know (C red, D orange, E yellow, F green, G teal, A blue, B magenta), a full rainbow so each white key is its own hue with C and F on opposite sides; an accidental wears its parent letter's exact color (F# = F green, Gb = G teal) — the half-moon notehead shape, not a hue shift, is what marks it as sharp/flat, so C# (a C, red) and Db (a D, orange) still read apart by letter (recovered from the engraving's notated letter). On the staff, an accidental's notehead carries a HALF-color overlay as a spelling cue: the standard black notehead (filled for a quarter, hollow for a minim) is drawn first so the note and its timing stay legible, then the letter's color fills 2/3 of it vertically with a 1/3 black strip — the strip on the left for a sharp (color on the right), on the right for a flat — split left/right (not top/bottom) so the notehead's full height stays intact and its level on the stave reads clearly, while naturals stay fully colored; an inspector toggle (or `fullcolor` token) switches accidentals to their full spelling color like the naturals. The coloring is always on across the whole piece, so a hue maps to a fixed place on the staff and keyboard. A **guidance keyboard** — a full-width 88-key NanoVG piano under the score, always on screen — lights the playhead window's still-troubled pitches in those same colors, each key fading with trailing clean plays of its source onset (3 clean = dark, one miss brings it back; drills always guide), so lining up hands with notation is a color match; the sheet coloring is always present, the key lights are the optional aid. A right/wrong verdict no longer recolors the note — a wrong note is flagged with a small red cross (white-haloed for contrast over any color) drawn over its head, correct notes are left alone. The score sits in a FIXED band — a share of the pane height (default 40%, adjustable by the inspector's `score height %` slider) — and the sheet scales to fill it (fitting the tallest window seen, so nothing clips off the bottom) and holds that scale and position, so it never resizes or jumps as the window scrolls (the waterfall fills the gap down to the keyboard). **Audition** (`p`, or a `notes` launch token): a simple three-partial synth sounds every note as the playhead crosses it — hear the score and play along — mixed into the metronome's output stream; the pill shows `notes`. **Waterfall** (the piano-roll band between the score and the keyboard, always on; `nowaterfall` disables): every note falls as a colored block toward its key — block height is the note's duration in beats, so the column reads directly as timing, and the bottom edge lands on the key exactly as the transport crosses the onset. Blocks wear the same spelling palette as the sheet and the keys, and a key lights full-bright while its note sounds, dimming when the block finishes; with the waterfall present the keyboard goes stumpy (the blocks carry the timing) and the key-lights fire on the beat (the falling blocks do the anticipating, so no key lights before its block lands). An **articulation** control (inspector slider, ~0.95 default) shortens each played note to that fraction of its notated length — with a floor of a couple pixels — so successive notes on one key show daylight and never light two-at-once in a run (drop it toward staccato for more separation). **Wrong-note marking** (`noverdict`/`nomistake` token, or the inspector checkbox): the wrong-note cross while keeping note colors; off leaves the sheet unmarked — the flow controller still scores regardless. **Composer toggle** (inspector checkbox, or `nocomposer` launch token) with **pedagogy sub-toggles**: fabricated **chord drills** and **scale fragments** are *opt-in* (inspector checkboxes under the Composer toggle, or the `drills`/`scales` launch tokens) while their teaching value is evaluated against real play — reviews, seams, fixes, the simplification ladder, spaced openings and overnight re-tests are always part of the program. The composer itself is **ON by default** — the adaptive program (reviews/drills/simplification/spaced openings/error re-serve) is the product; off scrolls the original piece bar-by-bar, unchanged. Sources the slicer cannot open (`.mxl` zips, multi-part scores) stream verbatim regardless. **Time-aware scheduling**: the player model stamps every complete pass with its civil day, and each session OPENS with yesterday's fumbled bars as *overnight re-tests* (sleep selectively consolidates the hardest transitions — they are re-tested, expected improved, not drilled from cold) followed by *spaced reviews* of clean bars whose day gap reached an expanding 1/3/7/14/30-day schedule keyed to day-separated clean streaks (minute-scale gaps within a session deliberately earn nothing). **Hand attribution** comes from the engraved staff (MEI, hand-crossing honoured) rather than a middle-C pitch split, which survives only as the fallback for staff-less notes. Toggling restarts the stream but **keeps the player's decided tempo** (learned pace or the tempo lock) — switching programs is not a reason to change their pace; only **Clear progress + restart** resets the tempo to the 60% ramp, because the learner's record itself was wiped. **ImGui learning inspector** (a floating panel, on by default, `` ` `` toggles it — the same self-hosted-context pattern the 3D hosts use): the design/debug/evaluate surface — transport (play/pause, rewind/restart, tempo slider + lock, mode + position), view toggles (waterfall + look-ahead beats + articulation, split sharps/flats, mark wrong notes, composer on/off), audio (metronome level, audition), live performance (accuracy EMA, score, streak, misses, wrong notes, key + analysis counts, plus a **Clear progress + restart** button that wipes this piece's record and starts fresh), timing drift (overall drag/rush plus the worst-drifting onsets — the triplet mechanic's raw signal), a **trouble tree** (worst bars, each expandable to left/right-hand right-wrong counts; troubled chords; most-missed pitches), the composer program (upcoming slots and their reasons), and bar-mastery counts. The convergence arc is mastery-gated: after the frontier finishes, the stream loops the weakest **phrase** until EVERY bar promotes — promotion demands **three consecutive clean complete passes** of the bar (a complete traversal with every note correct and no strays; the strongest retention predictor in the learning research is the share of complete-correct passes, and a quality *mean* would let a chronically fumbled note ride along) — then schedules the earned full performance run. A **fumbled pass re-serves its bar** — the **rewriting composer** splices the fix in *just past the playhead* (one guard bar covers the background window swap) and re-engraves the current window in place, so the correction reaches the player within a bar or two instead of a full engrave window; the sheet ahead of the playhead visibly updates while the current window keeps playing. Committed geometry never moves: the splice always lands beyond the playhead, so the stream-q-keyed verdict archive and the window carry are untouched. When the async engraver is unavailable or a swap is already in flight, the append path still catches the fumble at the next window, and each bar carries a persisted **tempo ladder**: clean passes raise its rung, fumbles lower it, and entering the bar caps the roll tempo at the earned rung — tempo rises only through demonstrated accuracy, while the tempo lock always wins. Practice chunks follow the music's own structure rather than a fixed window ([plans/scoreview-composer.md](../plans/scoreview-composer.md), from the learning-science evidence in [plans/scoreview-learning-research.md](../plans/scoreview-learning-research.md)): expert practice starts and stops at phrase/section boundaries, so arcs aim at detected phrases and only fall back to a fixed 8-bar slice when a piece's structure confidence is too low to trust (unphrased or atonal material, where the analysis refuses to invent phrasing rather than guess it). Serial position drives the repetition too — recall collapses from a phrase's opening bar toward its tail, so reviews prefer phrase tails over the cheap anchor bars at phrase openings, and a **seam** special serves the last bar of one phrase joined to the first of the next, the one join that arc practice (one phrase at a time) never crosses. Cooldowns and a piece-bars-between-specials floor keep it never boring; provenance maps review outcomes back to the source bar's statistics while drill outcomes train pitch/chord stats only; the pill shows DRILL/REVIEW inside special bars and `stream: slot N = ...` lines log every decision. **Piece analysis** (S1, at load): key estimation (Krumhansl–Kessler over an IOI-weighted pitch-class histogram, notated signature as prior, 8-bar windows catching modulations and section keys), a chord inventory with successor counts (waltz dyads borrow the bar's bass), recurring melodic motifs mined from the **sustain-aware skyline melody** (the top sounding pitch, where a ringing note shadows accompaniment struck beneath it, and for a beat after it ends an octave-plus drop is still accompaniment (the bass striking in a half-beat melodic breath), not a leap — per-onset top-pitch extraction would hear the oom-pah as melody in every gap; onsets carry per-note durations from the timemap to make this possible) with a bounded-depth suffix trie whose edges carry (interval, step rhythm in twelfths) — motifs are joint pitch+rhythm patterns of 4–8 notes, kept where they are maximal (their continuations diverge and their predecessors differ), with constant-shift occurrence families collapsed to their earliest window (a long repeating theme must not flood the kept set with overlapping copies of itself), never straddling a rest, ranked strongest-first with earliest-first tiebreak so the piece's opening gesture wins ties — and per-beat rhythm figures on a twelfth grid (triplets, dotted pairs, grace figures), plus the piece's **phrase and section structure** — phrase boundaries scored from cadences (read off the chord inventory's join table), breathing space across the barline, motif restarts, and modulations, with a `structure_confidence` that reports how much real evidence (never the 4-bar hypermetric prior, and never a boundary forced by the maximum phrase length) stood behind them, so the composer can tell detected phrasing from a guess; sections split only where the key actually moves, and an unmodulating piece is honestly one section — cached for the composer and dumped beside the progress file as `<hash>.analysis.json`. An **analysis overlay** (**`a`** in the reading view, the inspector View checkbox, or the `analysis` launch token) draws what the analysis discovered over the `'f'` paged reading view, all in green — the contract color for "Draxul's inference, not the engraved source": phrase spans above each system with their id and boundary confidence (`P4 ·0.67`), a taller, heavier opening tick where a section starts, key-region names with confidence above their first bar, every note of every motif occurrence **colored by its motif** through the same spelling-palette guidance channel the flow sheet uses (the seven Boomwhackers naturals, cycled by motif rank — same color = same melodic shape everywhere it recurs; a note claimed by two motifs keeps the stronger motif's color; chords tint only their top note, since motifs are mined from the melody line), with a banner legend of color swatches decoding each motif's occurrence count and interval shape, phrase **restatements** labelled where the analysis recognises repeated material (`P11 = P1` literal, `P11 ≈ P1` transposed — the melody's interval+rhythm shape matches, so a transposed return still reads as a repeat), and a corner banner summarising the whole profile (key, phrase count with how many are *unique*, sections, structure confidence, chord/motif/figure counts). A companion **unique-chunks view** (**`s`** in the reading view, inspector checkbox, or the `unique` launch token) ghosts every restated phrase under a near-opaque paper wash labelled with the original it repeats (`P11 = P1`), so what remains at full strength is the piece's actual unique material — the real amount there is to learn. Both toggles are independent; the wash draws under the green annotations when both are on. Annotation text is fixed UI-size (it never scales with the engraving) and labels measure themselves at draw time, nudging right past collisions (key changes coincide with phrase starts, so stacked labels are the norm). Geometry comes from true per-measure bounding boxes recorded by the SVG interpreter (every op inside a Verovio `<g class=\"measure\">` group contributes), so spans and washes run barline-to-barline over the full system band, with lanes hard-clamped into the inter-system gaps and below the page header. Paged-at-launch (`--command paged`) runs the piece analysis on demand if the conveyor never built it, so the overlay works without ever entering the runner. **Metronome** (on by default with eighth subdivisions — the runner's pace signal): position-locked to the transport, so it ticks at the adaptive tempo and pauses when the transport does; accented bar downbeats (bar length from the time signature), lower beat ticks, quieter subdivision eighths, synthesized inline and played through SDL; `t` cycles eighths -> off -> beats -> eighths (pill shows tick/tick8), `--command` tokens `notick`/`tick`/`tick8` override at launch. `--command roll-mic` starts the runner listening to the piano. **Gate (wait) mode** (dev/verification instrument, `--command gate`/`gate-bot`/`gate-bot-err` with keyboard, perfect bot, or 70%-accuracy bot): the transport waits at each onset, tempo adapts to gate-to-gate pace (EMA, stall decay), playhead turns teal while waiting — kept for bots and tests now that Roll is the game. **Acoustic listener core** (`NoteListener`, [plans/scoreview-ear.md](../plans/scoreview-ear.md)): offline-testable DSP that turns mono mic samples into note events for the gate — KissFFT STFT, energy-normalized spectral-flux onsets with a rise-spread beat gate, inharmonic partial-template scoring with per-piano tuning calibration (global EMA + per-note profile survives ±35-cent detuning), octave/twelfth phantom rejection via independence + excess-evidence tests, wrong-note reporting from an 88-key sweep; every tunable documented in `ListenerTuning`; verified against a deterministic synthetic piano (11 unit cases incl. chords, bass inharmonicity, sustain overlap, fast repeats, latency < 120 ms). **MIDI keyboard input**: the inspector's Transport section has an **input** dropdown — dev keyboard, microphone, or any connected **MIDI input port** (enumerated live via RtMidi: CoreMIDI on macOS, WinMM on Windows, hot-plugged devices appear when the combo reopens). MIDI is the lossless ground-truth input for tuning the composer before the acoustic listener is trusted: note-ons judge exactly like every other input (same `PlayerNoteEvent` currency, arrival-time-accurate timestamps mapped from the backend thread), switching input mid-session keeps verdicts/score/transport, and selecting a MIDI port auto-starts the transport (the piano is the interface). Played notes are also **voiced through the selected instrument** (most controllers are silent) — strikes carry MIDI velocity, releases damp the note like a real piano. **Instrument voice**: the Audio section's **instrument** combo picks between the built-in three-partial synth and any **.sf2 soundfont** staged beside the app under `soundfonts/` (loaded lazily on first selection via TinySoundFont; strikes and releases land sample-exact; both voices mix into the one output stream so switching never clicks). The build stages **FreePats' YDP Grand Piano** (a Yamaha Disklavier Pro sampled by Zenph Studios for OLPC, CC-BY 3.0 — the license text ships beside the .sf2); any other font dropped into `soundfonts/` simply appears in the combo (e.g. FreePats' Salamander Grand SF2, the reference free grand at a hefty 1.3 GiB). The instrument voices both the audition ('p') and MIDI play-thru. **Live microphone input** (`--command roll-mic`/`gate-mic`, or `i` inside either game mode to toggle mic <-> keyboard mid-session): SDL3 records the default input (f32 mono 44.1k) and `pump()` drains it through the listener; the device opens asynchronously with an AVFoundation TCC permission pre-flight (the consent dialog must never block SDL — launch stays live, shutdown never hangs), the status pill shows `MIC<level 0-9>` when capturing or `MIC?` while consent is pending, and a denied/unavailable device falls back to the keyboard with a WARN. The macOS bundle plist carries `NSMicrophoneUsageDescription` and the stable `com.draxul.draxul` identifier TCC keys on. Gated by `DRAXUL_ENABLE_SCOREVIEW`, default ON on Windows and macOS. |
| SatView | `--host satview` | Optional satellite-overview host with switchable interactive 3D globe, full-screen 2D equirectangular map, and Earth ground-observer sky views with default stereographic or conventional perspective projection, a hierarchical Sun/planet/major-moon POV dropdown for globe/map views, date-aware day/night lighting, a real-scale ephemeris-driven Moon with an 8k NASA LRO texture and analytical orbit track, a real-scale date-aware rotating Sun with an emissive 4k Solar System Scope texture, normalized planet/major-moon body views with selectable natural bodies, Sun-view per-planet orbit-track checkboxes, layered Saturn disk rings, a ray-marched Rayleigh/Mie atmosphere, a Hipparcos tiny-quad starfield with persisted apparent-magnitude window and brightness controls, optional constellation figures and an oriented 4k NASA Milky Way background, an elevated cloud shell using bundled clouds by default with an optional asynchronously cached near-real-time source, independently cached CelesTrak active-GP and SATCAT catalogs, precise SGP4 propagation plus clearly marked SATCAT summary estimates, population coloring/filtering for active payloads, inactive payloads, rocket bodies, debris, and unknown objects, all-sampled or selected-only path display, track/marker LOD controls, click and tree selection, an ImGui filter/details panel with a live simulation-clock readout in the user's local timezone, a `Real Time` action that restores the current system time at `1x`, smoothed quaternion left-drag orbit controls, Ctrl+drag and mouse-wheel dolly, MegaCity-style keyboard orbit/dolly controls, reset camera (`Home`), data refresh (`Ctrl+R`), panel toggle (`F1`), and time-speed controls (`Space`, `[`, `]`) |

Pane splits use the platform default shell (Zsh on macOS, PowerShell on Windows) regardless of primary host type.
Host names, aliases, platform support, test-only status, and split/new-workspace visibility come from the registered provider metadata. Optional hosts that are not built are therefore absent from the command palette and rejected explicitly by `--host`; the hidden `nanovg-demo` provider remains directly launchable by the render harness.

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
- **SatView HDR surface pipeline**: SatView renders every globe, map, and ground-view scene layer into a linear `RGBA16F` target with adaptive 4x, 2x, then 1x MSAA fallback across Vulkan and Metal. Multisampled color resolves into a single-sample HDR texture before ACES tone mapping, hardware sRGB encoding into `BGRA8 sRGB`, and UNORM-alias composition into the shared application backbuffer. Earth, Moon, Sun, and cloud textures use hardware sRGB decoding; star, track, and marker display colors are converted to linear before HDR blending. Persisted `Exposure` and `White point` controls tune the whole scene independently of the star-only brightness gain. An optional `SatView HDR Buffers` panel reports the requested and active sample counts and shows an MSAA sample-difference heat map, resolved HDR scene, and final tone-mapped image.
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
- **SatView Earth pass**: The optional SatView module renders a texture-mapped Earth through the shared 3D render-pass path on Vulkan and Metal, using staged 8k equirectangular day, night, and cloud maps with a UTC date-aware solar ephemeris, seasonal declination, Greenwich-sidereal-aligned TEME orbit tracks and satellite point markers, a quaternion-owned camera/manipulator with continuous pole-crossing orbit and dolly controls, a per-frame look-at view matrix, GPU marker billboards derived from the same camera quaternion, a generic binary Hipparcos star catalog rendered as additive instanced tiny quads behind globe and ground-sky views, and an orbit-aware dolly ceiling that expands to the visible satellite/orbit radius. Globe mode also renders the Moon at its analytical geocentric position, instantaneous Earth distance, and real radius using NASA's 8k LRO WAC color mosaic. The Moon has its own atmosphere-free diffuse pass, phase-dependent Earthshine, a stable IAU-pole-aligned tidal orientation, a visibility toggle, and an expanded camera ceiling that can frame the full Earth-Moon separation. Its independently toggleable pale orbit track spans one sidereal lunar cycle, uses the shared track-sample control, and remains stable until simulation time leaves the sampled half-month window; it is omitted from Moon map mode where a self-relative track has no useful projection. The same pass can switch to a six-vertex full-screen equirectangular map targeting either body and can enter an Earth ground-observer sky view by double-clicking the Earth globe or Earth map. Ground view places the camera just above the selected WGS-84 surface point, starts with a 60-degree angle of view and 0.1x marker scale, and defaults to a conformal stereographic projection with a 235-degree maximum while retaining the existing perspective projection with a 120-degree maximum. Its local quaternion camera crosses zenith, horizon, and nadir without an Euler pitch clamp. Stars, bodies, satellite markers, paths, picking, the optional full-screen Rayleigh/Mie atmosphere, and the Earth surface use matching forward or inverse projection math on Vulkan, Metal, and the CPU. Persisted `Show ground` and `Horizon occlusion` controls independently select the visible surface and whether below-horizon sky objects are filtered, allowing either a physical horizon or an unobstructed full-sky inspection. Earth POV projects markers to their current Earth subpoints and uses each orbit sample's own Earth-fixed position, producing temporal ground tracks rather than rotating an entire inertial orbit ring at one instant. Earth ground tracks remain open between their first and last sampled times, avoiding a false closing segment. Moon POV displays the lunar mosaic and projects every satellite and orbit point by its Moon-centered line of sight in the tidally locked lunar frame, so the Earth-orbiting catalog gathers around the Earth-facing hemisphere. Paired line endpoints and wrapped draw copies clip cleanly at both map edges without introducing internal path gaps. The map uses a rotated equirectangular projection whose center longitude/latitude can be changed without rebuilding or re-uploading the cached orbit buffer; surface sampling, solar lighting, markers, tracks, wrapping, and picking share the selected body's transform. Existing filters, colors, path modes, interpolation, selection, day/night lighting, and cloud source controls continue to apply. A separate atmosphere shell jointly ray-marches Rayleigh and Mie view/sunlight optical depth through 8 km molecular and 1.2 km aerosol scale heights, including wavelength-dependent extinction, Rayleigh and forward Henyey-Greenstein phase scattering, planet shadow, and premultiplied depth-aware composition. Clouds render on their own premultiplied shell about 9.5 km above the surface, giving them geometric separation and independent day/night lighting instead of painting them into the opaque Earth surface; in Earth map mode the selected cloud texture is composited directly over the flat Earth map. A background service downloads the latest 8k Live Cloud Maps EUMETSAT-derived texture and caches it for the source's three-hour update cadence. Bundled and live cloud maps keep separate GPU bindings: the bundled map is selected by default, `Realistic clouds` opts into the live map, and the master `Clouds` switch can disable the shell without discarding either texture. The controls show the advancing simulation date/time in the user's system timezone with its UTC offset. The `Real Time` button atomically unpauses, sets simulation time to the current system instant, resets speed to `1x`, and refreshes propagated positions. It is launched with `--host satview`.
- **SatView surface objects**: Moon globe and map POVs optionally display a bundled offline catalogue of 70 LROC-confirmed landers, rovers, instruments, retroreflectors, crewed artifacts, and impact sites grouped into 46 physical mission sites. LROC's IAU Moon 2000 east-positive coordinates are authoritative; GCAT CC BY 4.0 enriches mission identity, COSPAR/JCAT ids, owner, and country without overriding coordinate conflicts. Mars globe and map POVs also display a curated offline catalogue of nine landing sites for Viking 1/2, Mars Pathfinder, Spirit, Opportunity, Phoenix, Curiosity, InSight, and Perseverance using east-positive coordinates normalized to the shared surface catalogue format. Surface objects remain separate from propagated satellite state, use their own small Vulkan/Metal instance buffer and procedural kind symbols, share the same high-contrast marker palette on every body, depth-test against their body in globe mode, wrap at map edges, cluster to site representatives at global scale, and expand while zoomed in. The `Filter` panel has a separate `Surface` tab with one global type/quality checkbox set for all surface catalogues and a mission/site/object tree that follows the active or selected Moon/Mars body; clicking either a marker or tree row fills the Selection panel with coordinates, uncertainty, source, date, identifiers, and references, with a `Center on site` action.
- **SatView sky orientation overlays**: Persisted, independently controlled `Constellation figures`, `Constellation boundaries`, and `Constellation labels` overlays render in globe and ground views. Figures use the 656-segment Western stick-figure interpretation from ConstellationLines v1.3; constellation stick figures are not standardized by the IAU, so details such as branch points and included stars can legitimately differ from Stellarium's Modern sky culture and other charts. The separate official IAU boundaries use 4,936 tessellated J2000 segments and 89 ranked area-name anchors from the pinned BSD-3-Clause D3-Celestial catalog. Both Vulkan and Metal expand the celestial segments into stable screen-space quads, with pale solid figure lines and red solid boundaries. Separate persisted width controls from `0.5` to `8.0` pixels retain stable thickness under MSAA, DPI changes, perspective, and stereographic projection. Scene labels use Draxul's active FreeType/HarfBuzz font through a shared atlas builder, render in the HDR pass with a dark alpha halo, rebuild after font changes, and use deterministic rank-first collision removal that reserves cardinal labels plus projected Sun/Moon rectangles.
- **SatView observatory horizon**: Ground view has a persisted, default-on procedural observatory silhouette attached to the true local horizon in both stereographic and perspective projection. Its dome, telescope, radio dishes, equipment building, and mast are generated as independently clipped observer-local east/north/up primitives and translated by the observer-altitude horizon-depression angle, so their bases meet the physical ground without a continuous artificial horizon strip. Optional `N`, `E`, `S`, and `W` labels use the same scene-font atlas and remain fixed to their geographic azimuths; the silhouette remains available when the physical ground or horizon occlusion is disabled.
- **SatView Milky Way skybox**: A persisted, default-off `Milky Way background` control renders NASA SVS Deep Star Maps 2020 as the first scene layer behind stars, constellation lines, bodies, and satellites in globe and ground views. A persisted `Milky Way brightness` control scales the background from `0.0` to `1.0`, defaulting to the previous `0.55` intensity. The 4096x2048 plate-carree texture is sampled by reconstructed celestial direction rather than cube-face interpolation, so the ICRF/J2000 right-ascension/declination orientation remains aligned with SatView's Hipparcos catalog in perspective and stereographic projections on Vulkan and Metal. The source omits bright catalog stars, and the ray-marched ground atmosphere composites over the background using its optical-depth transmittance.
- **SatView Sun body**: In Earth and Moon views, SatView places the Sun at its date-aware geocentric direction and Earth-Sun distance with the nominal 695,700 km radius, so it has the expected roughly half-degree apparent diameter from Earth. Every normalized planet and major-moon globe view also renders the Sun at its true body-relative position, distance, and radius; a reversed-Z float depth buffer keeps depth precision near-logarithmic, so the far plane extends to interplanetary distances without z-fighting or bounded proxy spheres. A dedicated emissive HDR pass renders the 4k Solar System Scope texture with limb darkening, an IAU 7.25-degree axis orientation, and a rigid 25.38-day sidereal texture rotation. The persisted `Sun` visibility control applies to both geocentric and normalized contextual views. Selecting the Sun in the body dropdown switches to the normalized Solar-System view described below; the Sun itself has no galactic orbit track.
- **SatView Solar-System body views**: The POV control is a grouped dropdown covering the Sun, all eight planets, and 20 major moons. Earth and the Moon retain their precision satellite-catalogue paths; every other selection uses a normalized body-centric camera, a shared Vulkan/Metal textured ellipsoid renderer, source-based radii/oblateness and spin, and local mean-element orbit guides for the selected body's major natural satellites. Planet views render their major moons as small lit body geometry at their current local orbit positions in addition to marker overlays; single-clicking a natural body fills the `Selection` panel with its body data, and double-clicking enters that body POV. Major-moon views additionally show their textured, correctly phased parent planet at its true position and radius, and the reversed-Z depth buffer resolves its occultation of the contextual Sun from real geometry. The Sun view can zoom from its surface out through all eight planetary orbit guides, with persisted Mercury-through-Neptune checkboxes plus all/none controls in the `View` panel. Saturn uses layered annulus disks with band gaps for a more realistic ring silhouette, and Saturn-moon views place those depth-writing bands around the true-position parent Saturn; Uranus keeps a subtle line-band approximation. Planet surfaces use pinned Solar System Scope maps, suitable USGS/PDS global moon mosaics are bundled where available, and explicitly named deterministic procedural maps stand in for incompletely mapped moons. These local natural-body tracks are presentation-grade Kepler approximations, not Horizons/SPICE trajectories. No artificial satellite or debris orbit is shown outside the existing Earth/Moon catalogues unless a real trajectory source is imported.
- **SatView Earth and planet orbit tracks**: A persisted `Earth track` control draws Earth's date-aware heliocentric orbit around the rendered Sun in the legacy Earth/Moon scene path using the shared track-sample setting. In normalized Sun POV, persisted Mercury, Venus, Earth, Mars, Jupiter, Saturn, Uranus, and Neptune checkboxes filter the Solar-System orbit guides directly; the current all-visible default is preserved for existing users. These tracks are omitted from ground/Earth-map/Moon-map views where they would be irrelevant, and advancing simulation time does not rebuild or re-upload satellite paths unless the sampled window or visibility controls change.
- **SatView catalog service**: SatView parses and merges the CelesTrak `active` GP JSON feed with header-resolved RFC 4180 SATCAT CSV records by NORAD id. Active payloads retain their real mean elements; SATCAT supplies the broader split into active payloads, inactive payloads, rocket bodies, debris, and unknown cataloged objects plus owner, operational/data status, radar-cross-section, and central-body metadata. Current Earth-, Moon-, and Mars-centered `ORB` rows with no decay date are retained, then a bundled primary-source-backed lunar disposition overlay removes confirmed surface or impacted lunar objects which SATCAT still reports as orbiters; it currently corrects Chang'e-4 and HAKUTO-R M2. Landed, impacted, docked, decayed, and other-center records are excluded. Rows without enough orbital state remain selectable as explicitly non-rendered `Catalog only` entries; Mars SATCAT rows currently stay catalog-only unless a real trajectory source is imported. GP and SATCAT use independent last-good payload/metadata caches, two-hour and twelve-hour freshness guards respectively, and independent failure handling, so one unavailable source does not discard the other. Offline startup merges both caches when available and falls back to the bundled sample only when neither catalog is usable. This covers individually cataloged objects only, not small untracked fragments or dust.
- **Native network transport**: Weather, SatView catalog, and live-cloud downloads use a shared bounded HTTP client backed by WinHTTP on Windows and `NSURLSession` on macOS. Requests have explicit connection and overall deadlines, per-service response-size limits, RFC 3986 query encoding, and cancellation before worker joins, so runtime networking no longer requires `curl` or passes URLs through a command shell. Weather responses are parsed as typed JSON and reject missing, non-finite, wrong-type, or out-of-range values.
- **SatView propagation service**: SatView keeps active Earth GP records on the pinned Vallado/CelesTrak AIAA-2006-6753 SGP4 reference implementation. Earth SATCAT-only records never enter SGP4: a separate deterministic summary solver reconstructs an ellipse from period/inclination/perigee/apogee, derives stable orientation and phase from the NORAD id, and labels every resulting marker and selected path `SATCAT summary estimate`. Moon and Mars SATCAT summary fields are not treated as body-relative orbital elements; Mars rows remain catalog-only until a sampled Mars-relative trajectory source is imported. A generated `lunar_ephemeris.csv` currently upgrades six missions for 1–19 July 2026: LRO, CAPSTONE, Danuri, and Chandrayaan-2 Orbiter use NASA/JPL Horizons geometric Moon-centred ICRF vectors; ARTEMIS P1/P2 use explicitly labelled NASA SSC six-year predictions converted from geocentric GEI J2000 with matching JPL Moon vectors. Position and velocity samples use cubic Hermite interpolation, mission-appropriate track windows, and strict validity bounds with no extrapolation. The pinned manifest and generator make later refreshes reproducible. Completed states and tracks retain catalog order, central body, and solution fidelity through the same backend-neutral Vulkan/Metal scene stream.
- **SatView dock panels**: SatView owns six pane-local ImGui dock panels toggled together by `F1`. The `Scene` panel owns the actual GPU render viewport, so camera projection, labels, map panning, ground-entry rays, and picking follow its docked content rectangle instead of rendering underneath control windows. On startup, `Scene` fills the main dock while `View`, `Rendering`, `Filter`, `Selection`, and `About` are stacked as tabs in a left dock; every panel can then be rearranged through normal ImGui docking. `View` owns pause, real-time, camera reset, refresh, and defaults actions; globe/map/ground mode; body POV; the central-body catalogue choice; object/path visibility; global surface visibility; Sun-view planet-track checkboxes; star magnitude limits; and constellation-overlay enables. `Rendering` owns tone mapping, colors, brightness, line widths, marker scale, and path/marker quality limits. `Filter` splits orbit and surface filters into separate tabs: `Orbits` owns search, orbit/source/age/population/body filters, counts, and the SATCAT/GP object tree; `Surface` owns one global surface kind/quality filter set and the active or selected body's surface object tree. `Selection` contains propagated satellite details, catalogue-only object details, surface-object details, or selected natural-body facts with a `Go to body` action, while `About` contains catalogue, cloud, source, and rendered-count status. Selecting Moon POV always selects Moon objects, selecting Mars POV selects Mars objects, and selecting Earth POV selects Earth objects, including when reselecting the current POV after manually changing the body filter. Body orbit tracks remain contextual: the Moon's Earth orbit appears from Earth POV and Sun POV has individually toggleable planetary orbit guides, while neither irrelevant/self track clutters Moon POV.
- **SatView sun-synchronous filter**: SatView derives a persistent `SSO candidates only` filter from each orbit's first-order J2 nodal-precession rate, using current GP elements when available and SATCAT period/inclination/perigee/apogee summaries otherwise. SSO remains an independent property layered on the existing LEO/MEO/GEO/HEO class, is shown in selected-object details, and intentionally includes debris or rocket bodies that still occupy a matching sun-synchronous plane. When enabled it can be split into `Dawn/dusk (terminator)` and `Other SSO` sub-filters (persisted as `show_sun_synchronous_terminator` / `show_sun_synchronous_other`): dawn/dusk orbits hold a local time of the ascending node near 06:00/18:00 and ride the terminator, while the rest (morning/afternoon and noon/midnight) pass through Earth's shadow each orbit. The dawn/dusk classification is derived from the ascending node's angle to the Sun at epoch (GP) or the modeled sun-synchronous node (SATCAT summaries), and the selected-object panel reports which class an SSO belongs to.
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
- **SatView camera/map/ground**: In globe mode, left-drag orbits and `Ctrl` + left-drag/mouse wheel dollies; `A/D`, left/right arrows, or `Q/E` orbit horizontally, `W/S`, up/down arrows, or `T/G` orbit vertically, and `R/F` dolly in/out. In map mode, left-drag pans the projection while the same horizontal/vertical keys move its center longitude/latitude. Double-clicking the Earth globe or Earth map enters ground view; double-clicking a rendered natural body in normalized planet views switches POV to that body. In ground view, left-drag and the same horizontal/vertical keys rotate a local quaternion continuously across zenith and nadir, mouse wheel or `R/F` changes angle of view, and the panel provides stereographic/perspective and Back to Globe/Map controls.
- **Smooth scroll**: Trackpad momentum accumulation (configurable speed multiplier)
- **File drop**: Native drag-and-drop dispatched to host as `open_file:` action
- **Kanban navigation**: Kanban panes support Vim-style card selection with `h/j/k/l`, shifted `H/J/K/L`/arrow movement for reordering cards and moving files between columns, `r` reload, and Enter to open the selected Markdown card.
- **GUI keybindings**: Chord-style prefix bindings (e.g. `ctrl+s, |`)
- **Command palette**: `Ctrl+Shift+P` opens a centered fuzzy-search overlay for all GUI actions with fzf-style scoring, `Ctrl+J/K` navigation, keybinding hints, and palette-rendered text prompts for actions needing short values
- **Print pane** (`print_pane` action, palette or `[keybindings]`): captures the focused pane's pixels, composes a single-page A4 PDF (aspect-fit inside margins, auto landscape for wide panes, CoreGraphics), and presents the native macOS print dialog for it (PDFKit print operation: preview, printer/paper choice, and auto-rotation so landscape pages land correctly on portrait paper); toasts report printed/canceled/failed. Hosts advise the printer via `IHost::print_hint()` — a pane-relative content rect plus a paper-white flag — so ScoreView prints just the page/band (no backdrop border) with its warm screen sheet tint snapped to pure white instead of printed stipple. macOS-only for now. `DRAXUL_PRINT_DRY_RUN=1` composes the PDF but skips the dialog and toasts the temp path (test hook)
- **`--gui-action <name>` CLI test hook**: with `--screenshot`, pumps until content is ready, dispatches any canonical GUI action by name, then captures — lets headless runs exercise palette actions and verify their toasts/effects
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
| `--host <type>` | Host type: nvim, markdown, powershell, bash, zsh, wsl, megacity, bioview, satview, score |
| `--command <cmd>` | Override host command path |
| `--source <path>` | Markdown file for `--host markdown`; Tree-sitter scan root for `--host megacity` or `--host bioview`; MusicXML or `.mxl` score for `--host score` |
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
- Normal Debug/Release presets explicitly disable coverage and sanitizers, and the test scripts reject an instrumented shared cache before running. This prevents a prior coverage/ASan/TSan configure from silently slowing or changing the ordinary unit workflow
- `do smoke` remains the explicit startup check; the individual render shortcuts and `renderall` remain the explicit visual checks. `t.sh`, `t.bat`, and `scripts/run_tests.*` retain the full unit + smoke + available render-snapshot workflow for pre-commit, release, and CI validation
- `do run release --host megacity --parser treesitter` strips the helper flag before launching, writes `[mega_city_code].code_source = "treesitter_db"`, and removes stale `graphify_graph_path` entries from that section. `--parser treesitter_db` is accepted as the same helper alias
- `do deploy` creates a Release build, stages the runtime payload into `deploy/YYYY_MM_DD/mac` or `deploy/YYYY_MM_DD/win`, and writes a matching `draxul-YYYY_MM_DD-mac|win.zip` archive under the date folder. Windows packages contain only `draxul.exe`, its Microsoft C++ and adjacent runtime DLLs, compiled shaders, bundled fonts, and runtime assets; CMake metadata, object files, static libraries, tests, and source/build directories are excluded
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
| `DRAXUL_ENABLE_SCOREVIEW` | ON on Windows/macOS | ScoreView optional module (`modules/score/`) with the pinned Verovio engraving runtime |
| `BUILD_TESTING` | ON | Test targets |

Markdown and Kanban are product modules under `modules/markdown/` and `modules/kanban/`. They are built by default and keep their existing host flags and CMake target names.

### Build Targets
- `draxul` -- Main executable (.app bundle on macOS)
- `draxul-tests` -- Unit test suite (Catch2), compiled with a test-only precompiled header and registered as four disjoint CTest shards labeled `unit`
- `draxul-rpc-fake` -- Fake RPC server for integration tests

ScoreView builds as five libraries (all inside the `DRAXUL_ENABLE_SCOREVIEW` gate): `draxul-score-learn` — the GPU-free learning core (player model, piece analysis, source slicer, the `IComposer` seam with the adaptive `StreamComposer`, `StreamProgram` provenance, the MusicXML measure writer, progress-file IO; links only tinyxml2 + nlohmann_json, so composers provably cannot reach transport/layout/audio/device code); `draxul-score-input` — the player-input seam, dev keyboard, and hardware MIDI input (isolates rtmidi); `draxul-score-audio` — metronome/tone synths, soundfont voice, and the acoustic note listener (isolates tinysoundfont + kissfft; pure DSP, no SDL); `draxul-scoreview` — layout/transport (Verovio wrapper, SVG interpreter, flow judge, verdict archive, bot input); `draxul-scoreview-host` — the IHost layer with the internal ScoreAudioController/ScoreSessionController/ScoreStreamController components, the ScorePresentation frame composer fed by a per-frame ScoreViewModel (inspector mutations flow through deferred intents), the input rig, and the microphone front-end.

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
