# ScoreView — Music Score Host (Master Plan)

*Created 2026-07-10. Research background: [music-notation-research.md](music-notation-research.md).*

> **North star:** this module's end goal is the adaptive piano-learning
> endless runner described in the
> **[ScoreView Manifesto](scoreview-manifesto.md)** — a game that teaches you
> a chosen piece through a never-stopping, tempo-adaptive stream of real
> notation, heard through the microphone. The phases below built the
> rendering foundation; future phases (flowing single-row view, timemap
> highlighting, the selection engine, audio matching) serve the manifesto.

## Goal

A new Draxul host that renders a reasonably complex piece of piano music,
loaded from MusicXML, cleanly and beautifully in the window.

**v1 target (this plan):** `draxul --host score <piece.musicxml>` shows an
engraved, paged score — fit to window width, vertical scroll, crisp at any
DPI. That's it.

**Explicit non-goals for v1** (follow-on phases, listed at the end, not
designed here): editing, playback, note highlighting, timemaps, MIDI import,
flowing single-system mode.

## Architecture

```
piece.musicxml ──► draxul-notation            (semantic model + importer; pure, no deps beyond tinyxml2)
      │
      └──────────► ScoreLayoutEngine (Verovio, pimpl'd)   ──► SVG string per page
                          │
                   SvgScoreInterpreter (tinyxml2 + path parser)
                          │
                   ScoreDrawList  (symbols, glyph instances, paths, lines — glm types)
                          │
                   ScoreHost::draw() ──► INanoVGPass callback ──► Metal/Vulkan
```

Two parallel consumers of the file in v1: our semantic model (the future
editing document) and Verovio (layout). They are **not** bridged yet —
model↔layout ID mapping is a later phase and is the known hard seam
(documented under Follow-ons).

The layout engine sits behind an interface (`ILayoutEngine`) so Verovio is
replaceable by a custom piano-scoped engine later without touching the model,
interpreter contract, host, or renderer.

## Module layout (mirrors `modules/markdown/`)

```
modules/score/
├── CMakeLists.txt                       # add_subdirectory guards
├── draxul-notation/                     # PURE: semantic model + MusicXML import
│   ├── include/draxul/notation/
│   │   ├── score_document.h             # ScoreDocument, Part, Measure, Note, Pitch, Fraction
│   │   └── musicxml_importer.h
│   ├── src/
│   │   ├── score_document.cpp
│   │   └── musicxml_importer.cpp        # tinyxml2-based
│   └── CMakeLists.txt
└── draxul-scoreview/
    ├── include/draxul/scoreview/
    │   ├── layout_engine.h              # ILayoutEngine + LayoutOptions
    │   ├── verovio_layout_engine.h      # pimpl — no verovio types in header
    │   ├── score_draw_list.h            # ScoreDrawList structs (glm)
    │   ├── svg_score_interpreter.h      # Verovio-SVG dialect → ScoreDrawList
    │   ├── score_render_nvg.h           # ScoreDrawList → NVGcontext replay
    │   └── score_host.h                 # ScoreHost : IHost
    ├── src/ (matching .cpp files)
    └── CMakeLists.txt                   # two targets: draxul-scoreview (GPU-free),
                                         #              draxul-scoreview-host (host + nanovg)
```

Target split rationale: `draxul-scoreview` (layout engine + interpreter +
draw list) stays GPU-free and unit-testable in `draxul-tests`;
`draxul-scoreview-host` links `draxul-host`, `draxul-renderer`,
`draxul-nanovg` — same split as `draxul-markdown` / `draxul-markdown-host`.

## Key decisions

1. **XML parser: tinyxml2, not pugixml.** Verovio compiles its own vendored
   pugixml into its static library; linking a second stock pugixml would be
   an ODR/duplicate-symbol hazard. tinyxml2 (zlib license, one .cpp) avoids
   the collision entirely. Used by both the importer and the SVG interpreter.
2. **Durations are exact rationals** (`Fraction {num, den}` of a whole note),
   never floats. MusicXML `divisions` can change mid-file; the importer
   normalizes to rationals once. Onset times within each measure are
   **computed once at import and stored on the note** (handles
   `backup`/`forward` voice interleaving); consumers never re-derive them.
3. **Render via the existing `INanoVGPass`** ([nanovg_pass.h](../libs/draxul-nanovg/include/draxul/nanovg_pass.h)),
   replaying the draw list each dirty frame. NanoVG re-tessellates per frame;
   for a static score with dirty-flag redraws (markdown-host pattern) this is
   fine for v1. If dense pages make scroll redraws slow, the escape hatches
   are, in order: cull draw ops to the visible page range; cache glyph
   outlines as pre-rasterized atlas quads via TextService + Bravura; custom
   render pass (markdown precedent exists). Do not build these until measured.
4. **Paged layout v1** (Verovio default breaks), fit-width, vertical scroll.
   The flowing `breaks: none` single-system mode is a later product phase.
5. **Verovio pinned to a release tag**, importers we don't need compiled out
   (Humdrum/ABC/PAE — verify exact CMake option names at implementation).
   LGPL-3 is fine for this repo's builds; revisit linking if we ever ship.
6. **Naming**: module `modules/score/`, host `ScoreHost`, `HostKind::Score`,
   CLI `--host score` (alias `scoreview`), gate `DRAXUL_ENABLE_SCOREVIEW`
   (default ON) — mirroring the SatView pattern.

## Core types (sketch)

```cpp
// draxul-notation — symbolic, renderer-free
struct Fraction { int num = 0; int den = 1; };      // whole-note units, always reduced
enum class Step { C, D, E, F, G, A, B };
struct Pitch { Step step; int alter; int octave; };  // alter −2..+2, octave 4 = middle C octave

struct Note {
    uint64_t id;              // stable within document, assigned at import
    bool is_rest;
    Pitch pitch;              // valid when !is_rest
    Fraction onset;           // within measure — computed once at import
    Fraction duration;
    int dots;
    int staff;                // 1-based within part (piano: 1 = upper, 2 = lower)
    int voice;                // 1-based
    bool chord_with_prev;     // MusicXML <chord/> flag
    TieState tie;             // None/Start/Stop/Both
    std::optional<Accidental> written_accidental;
    std::optional<TimeModification> tuplet;   // actual/normal note counts
};
struct Measure { std::vector<Note> notes; std::optional<KeySig> key; std::optional<TimeSig> time; std::vector<ClefChange> clefs; };
struct Part { std::string name; int staves; std::vector<Measure> measures; };
struct ScoreDocument { std::string title, composer; std::vector<Part> parts; };
```

```cpp
// draxul-scoreview
struct LayoutOptions { glm::ivec2 page_size_px{0}; float pixel_scale = 1.0f; };
class ILayoutEngine {
public:
    virtual ~ILayoutEngine() = default;
    virtual bool load(std::string_view musicxml_bytes, std::string& error) = 0; // .mxl bytes OK (Verovio unzips)
    virtual void set_options(const LayoutOptions& opts) = 0;                    // triggers re-layout
    virtual int page_count() const = 0;
    virtual std::string render_page_svg(int page) = 0;
};

struct PathCmd { enum class Op { MoveTo, LineTo, CubicTo, Close }; glm::vec2 p, c1, c2; };
struct SymbolOutline { std::string id; std::vector<PathCmd> cmds; };            // from <defs> (one per distinct SMuFL glyph)
struct GlyphInstance { uint32_t symbol_index; glm::vec2 pos; glm::vec2 scale; std::string element_id; };
struct FilledPath   { std::vector<PathCmd> cmds; std::string element_id; };     // beams, slurs, ties
struct StrokedLine  { glm::vec2 a, b; float width; std::string element_id; };   // staff lines, stems, barlines
struct ScoreDrawList {
    std::vector<SymbolOutline> symbols;
    std::vector<GlyphInstance> glyphs;
    std::vector<FilledPath> paths;
    std::vector<StrokedLine> lines;
    glm::vec2 page_size{0.0f};
};
```

`element_id` is carried from Verovio's SVG on every op now (cheap) because it
is the future hook for hit-testing and playback highlighting.

## Phases

### Phase 0 — Skeleton host on screen ✅ (2026-07-10)

- [x] Add `HostKind::Score` to [host_kind.h](../libs/draxul-types/include/draxul/host_kind.h) (`parse_host_kind`: "score" | "scoreview"; `to_string`)
- [x] `modules/score/` CMake skeleton; `DRAXUL_ENABLE_SCOREVIEW` option (default ON) + `add_subdirectory` guard in top-level CMakeLists (mirror SatView at lines ~34/149)
- [x] `ScoreHost : IHost` minimal: initialize/pump/draw/viewport/status_text; owns an `INanoVGPass`, records it in `draw()` — [score_host.cpp](../modules/score/draxul-scoreview/src/score_host.cpp)
- [x] `register_score_host_provider()` called from `app/main.cpp` under `#ifdef DRAXUL_ENABLE_SCOREVIEW`
- [x] Proof-of-life draw: A4 page with drop shadow + grand staff (two staves at SMuFL proportions: 0.13sp lines, thin/thick final barline pair), 4 placeholder measures, hand-tuned Bézier brace — all pixel_scale-aware
- [x] Acceptance: `py do.py smoke` passes; `draxul --host score --source tests/fixtures/musicxml/grieg-waltz-op-12-no-2.mxl --smoke-test` exits 0 (Metal init + frames + clean shutdown); full `ctest` suite green. On-screen eyeball check: pending user's next launch

### Phase 1 — Semantic model + MusicXML importer (`draxul-notation`) ✅ (2026-07-10)

- [x] Add tinyxml2 to `cmake/FetchDependencies.cmake` (pinned 10.0.0, static)
- [x] Model types per sketch (+ `Fraction` arithmetic: add/subtract/compare, normalized, long-long intermediates) — [score_document.h](../modules/score/draxul-notation/include/draxul/notation/score_document.h)
- [x] Importer: partwise documents; header (title/composer/part-list); `divisions`, `attributes` (key/time/clefs/staves), `note` (pitch/rest/unpitched/grace/chord/duration/dots/staff/voice/tie/accidental/time-modification), `backup`/`forward` — [musicxml_importer.cpp](../modules/score/draxul-notation/src/musicxml_importer.cpp)
- [x] Onset computation with backup/forward + chord/grace handling; stored on note; measure-fill validation warns (implicit pickups exempt), never rejects
- [x] Reject `timewise` and compressed `.mxl` input with clear errors
- [x] Unit tests: 12 cases / 146 assertions in [notation_importer_tests.cpp](../tests/notation_importer_tests.cpp) — Fraction math, single note + header, mid-file divisions change, chords, two-staff backup voices, dotted/tuplet durations, grace notes, ties/accidentals, fill warnings, error paths
- [x] Acceptance: Grieg fixture imports with **zero warnings**; every independently-counted ground-truth stat matches (79 measures, 694 notes, 49 rests, 14 grace, 202 chord members, 36 tuplet notes, 36 tie elements, 49 accidentals, 65 dots, 4 clef changes, 4 key changes, voices {1,2,5}, unique ids, all onsets within 3/4). Extracted `grieg-waltz-op-12-no-2.musicxml` checked in alongside the `.mxl`

### Phase 2 — Verovio layout engine (`draxul-scoreview`) ✅ (2026-07-10)

- [x] FetchContent verovio pinned to `version-6.2.1`, `SOURCE_SUBDIR cmake`, built via upstream `BUILD_AS_LIBRARY` (**shared** lib — upstream's supported embedding path and the LGPL-friendly mode; the plan's "static" idea was amended). Humdrum/ABC/PAE/GABC importers compiled out; MusicXML + `.mxl` kept; headers SYSTEM; consumer TU carries matching `NO_*_SUPPORT` defines (vrv headers branch on them); `include/tuning-library` needed since 6.1 ASCL support
- [x] Runtime resources staged by `stage_verovio_data` to `<exe-dir>/verovio-data` on all platforms (mirrors megacity/satview asset staging); tests read `${verovio_SOURCE_DIR}/data` via `DRAXUL_VEROVIO_DATA_DIR`
- [x] `VerovioLayoutEngine : ILayoutEngine` (pimpl, verovio types only in the .cpp) — [verovio_layout_engine.cpp](../modules/score/draxul-scoreview/src/verovio_layout_engine.cpp): `page_size_px`/`pixel_scale` → pageWidth/pageHeight + scale (base 40%, hidpi folds into scale), `footer: none`, `svgViewBox: true`; `.mxl` via zip-magic → `LoadZipDataBuffer`, XML via `LoadData`; `set_options` re-layouts via `RedoLayout`
- [x] Unit tests: 4 cases / 31 assertions in [scoreview_layout_tests.cpp](../tests/scoreview_layout_tests.cpp) — missing resources, minimal score (page count, `<svg`/`viewBox`/`<use`/staff, page-range guards), garbage input, Grieg `.mxl` end-to-end + shrink reflow (page count grows)
- [x] Acceptance + baseline: full Grieg `.mxl` load + layout + SVG render of all pages + full reflow = **0.16 s wall clock including process startup** (M4 Pro, debug build); per-operation timings logged at DEBUG. Verovio notes the fixture has 1 unterminated tie (source-file quirk, harmless)

### Phase 3 — SVG interpreter → ScoreDrawList ✅ (2026-07-10)

The real dialect (studied from live 6.2.1 output before writing code) differs
usefully from the plan sketch: defs are plain `<g>` (not `<symbol>`) holding
font-unit outlines pre-flipped by `scale(1,-1)` (baked into the stored
outline), `<use>` placement is transform-based (`translate(…) scale(…)`), so
glyph instances carry a full 2x3 affine; dots are `<ellipse>`, beams
`<polygon>`, hairpins `fill="none"` `<polyline>`, text is class-styled tspans
(dynam/dir/mNum → italic, tempo/fing/reh/ending → bold per Verovio's own
emitted CSS). Draw list generalized accordingly: `GlyphInstance` (affine),
`DrawPath` (fill and/or stroke), `DrawText` — see
[score_draw_list.h](../modules/score/draxul-scoreview/include/draxul/scoreview/score_draw_list.h).

- [x] Path-data parser: `M L H V C S Z` + relative forms, implicit repetition, smooth-cubic reflection; fails loudly on `Q/A/T` (Verovio never emits them) — exposed for unit tests
- [x] Interpreter over the dialect: g-transform/id/style context stack; `<use>` → affine glyph instances; path/rect/polygon/polyline/ellipse/line → pre-transformed `DrawPath`s (fill heuristic: only closed-or-curved shapes fill, keeping open bracket runs from becoming solid triangles); styled text runs; viewBox → canvas/pixel sizes; unknown constructs recorded once per kind — [svg_score_interpreter.cpp](../modules/score/draxul-scoreview/src/svg_score_interpreter.cpp)
- [x] Checked-in fixture [tests/fixtures/svg/verovio-minimal-c4.svg](../tests/fixtures/svg/verovio-minimal-c4.svg) with exact-coordinate assertions (symbol y-flip baking, page-margin translate, element-id attribution, stroke widths) — the version-drift tripwire
- [x] Acceptance: **zero unhandled constructs across all live Grieg pages**; 8 scoreview test cases / 129 assertions green (includes phase-2 suite)

### Phase 4 — ScoreHost renders the piece ✅ (2026-07-10)

- [x] Loads `--source` bytes once: semantic model import (best-effort — `.mxl` is engine-only until the model importer grows zip support) + Verovio engine load; resources resolved via `executable_directory()/verovio-data`; init failures surface through `init_error()`
- [x] Fit-width layout on viewport/zoom changes (`layout_dirty_` + relayout in `pump()`), pages re-interpreted into a shared_ptr snapshot the draw callback captures safely
- [x] [score_render_nvg.cpp](../modules/score/draxul-scoreview/src/score_render_nvg.cpp): draw-list replay (paths fill/stroke, glyph affine transform replay, serif text via system Times/Georgia with italic/bold faces), shared white-sheet + shadow helper; only pages intersecting the viewport render
- [x] Vertical scroll (wheel/trackpad, `j`/`k`, arrows, PageUp/PageDown/Space, Home/End) with clamping; zoom via `font_increase`/`font_decrease`/`font_reset` actions (0.4–4.0x, re-engraves)
- [x] `status_text()`: title — composer (model metadata when available, filename otherwise) + `p. n/m` + zoom; INFO log per relayout (`score: engraved N page(s), M draw ops`); no-source placeholder retained
- [x] [docs/features.md](../docs/features.md) updated to the real feature description
- [x] Acceptance: Grieg engraves and renders in-app — verified via `--smoke-test` (`score: engraved 1 page(s), 3083 draw ops (2268px wide, zoom 100%)`, clean exit); full `ctest` + `py do.py smoke` green. On-screen eyeball pass: user's next launch
- [x] Post-eyeball fixes (first on-screen review): glyph counters rendered
  solid (NanoVG fills every subpath unless marked — subpath orientation is now
  classified against the dominant contour at interpretation time and replayed
  as `NVG_HOLE`); the metronome-note tspan (`font-family="Leipzig"`, PUA
  codepoint) rendered as a .notdef box through Times (Leipzig.ttf is now
  staged with the Verovio resources and loaded as the music text face); and
  sibling tspans of one `<text>` all drew at the same anchor (continuation
  runs now chain by measured advance)
- [ ] Stretch (open): `draxul-render-score` snapshot scenario + bless entry

## Testing summary

- Model/importer: pure Catch2 tests, fixture-driven (Phase 1 list)
- Layout engine: smoke-level (loads, produces SVG) — we don't test Verovio's engraving, we pin it
- Interpreter: canned-SVG fixture tests — the version-drift tripwire
- Host: smoke + existing suites; snapshot scenario as stretch
- Validation per repo norms: build `draxul draxul-tests`, `py do.py smoke`, `ctest` before commits

## Risks

| Risk | Mitigation |
|---|---|
| Verovio compile time bloats builds | Static lib built once per configure; importers trimmed; module gated by `DRAXUL_ENABLE_SCOREVIEW` |
| SVG dialect drift on Verovio upgrades | Version pinned; interpreter fixture tests fail loudly on drift |
| pugixml ODR clash | Avoided by decision #1 (tinyxml2 everywhere on our side) |
| MusicXML dialect sloppiness (divisions changes, overfull measures, missing types) | Importer is tolerant + WARN-logging; fixtures encode the weird cases as we meet them |
| NanoVG tessellation cost on dense pages | Dirty-flag redraws; page culling; measured before optimizing (decision #3 escape hatches) |
| Verovio resource dir missing at runtime | Bundled at build time both platforms; clear init_error if not found |

## Sample piece

`tests/fixtures/musicxml/grieg-waltz-op-12-no-2.mxl` — Grieg, Waltz Op. 12
No. 2 (compressed MusicXML, ~295 KB uncompressed; container + score XML).
Good coverage: two-staff piano, key/tempo changes, articulations, dynamics.
Worth adding later: one very clean baseline (e.g. Bach BWV 846 Prelude) and
small hand-authored snippets for importer unit tests (the W3C MusicXML
sample suite is a good source).

## Follow-on phases (recorded, deliberately not designed yet)

*(Priority ordering now follows the [manifesto](scoreview-manifesto.md): the
flowing single-system view, the model↔layout bridge, and timemap
highlighting form the spine of the endless runner; microphone note-matching
and the adaptive selection engine come next.)*

- **Model↔layout ID bridge** — serialize our model to MusicXML/MEI with
  stable IDs so Verovio's element IDs map back to model notes. Prerequisite
  for highlighting and editing. This is the hard seam; design when we get here.
- **Timemap + playback highlight / Flowing mode** — ✅ shipped as manifesto
  milestone 1: [scoreview-conveyor.md](scoreview-conveyor.md) (flowing
  single-row view, transport clock, timemap-driven light-up).
- **Wait-mode + judgment + adaptive tempo + score** — ✅ shipped as manifesto
  milestone 2: [scoreview-gate.md](scoreview-gate.md) (the game loop behind
  an `IPlayerInput` seam; wait-mode is now a dev/verification instrument).
- **The acoustic listener** — manifesto milestone 3:
  [scoreview-ear.md](scoreview-ear.md) (microphone → note events;
  verification-not-transcription, inharmonic templates, tuning calibration;
  includes the recognition notebook of acoustic facts and tuning knowledge).
  E0–E2 ✅; E3 (tuning against the real piano) pending.
- **Roll mode — the runner** — ✅ R0/R1 shipped as manifesto milestone 4:
  [scoreview-runner.md](scoreview-runner.md) (Guitar Hero for piano: the
  transport never waits, timing-window judgment, accuracy-driven tempo;
  the default game). R2 feel pass with the user pending.
- **The stream — dynamic music + player memory** — manifesto milestone 5,
  planned: [scoreview-stream.md](scoreview-stream.md) (rolling N-bar
  window with dynamic append, piece analysis — key/chords/motifs/rhythm
  figures, persistent JSON player model with timing statistics, and the
  composer that drills what the player struggles with, converging on the
  whole piece over a long session).
- **Editing** — command-based model mutations, hit-testing via element_id,
  re-layout loop; MIDI step input.
- **Glyph atlas optimization** — Bravura through TextService if NanoVG
  replay ever measures too slow.
- **Windows enablement** — ✅ completed 2026-07-13: ScoreView now defaults ON
  on Windows as well as macOS. Verovio's shared-library target exports and
  links successfully with MSVC; its DLL is staged beside `draxul.exe`, DSP
  code uses portable C++20 pi constants, and live input uses SDL's default
  WASAPI capture device.
- **Route Verovio's stderr logging** into draxul's log system (it prints
  `[Warning]`/`[Error]` lines directly; fine for tests, noisy for the app).

## Build-system incident notes (2026-07-10)

A machine-destabilizing build storm traced to three compounding defects, all
fixed the same day:

1. `do.py` passed a bare `--parallel` to `cmake --build`, which the Makefiles
   generator maps to an **unbounded** `make -j`. Interrupting that build
   orphaned hundreds of in-flight clang jobs. Fixed: bounded to CPU count
   (`_parallel_jobs()`).
2. Verovio's CMake defaults itself to Release when `CMAKE_BUILD_TYPE` is
   empty, so configures reaching it with/without a concrete type flip its
   ~300 objects between `-O3` and `-g` wholesale. Fixed: top-level guard sets
   a concrete build type before any dependency configures.
3. Verovio's `get_git_commit.sh` rewrites `include/vrv/git_commit.h` with a
   fresh timestamp on **every configure**, dirtying `vrv.cpp` + the library
   link each time. Fixed: the fetched script is replaced post-population with
   a create-once guard (the tag is pinned; the commit can't change).

Verified: two consecutive `cmake --preset mac-debug` configures now leave
zero stale objects.
