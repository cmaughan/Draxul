# ScoreView — Music Score Host (Master Plan)

*Created 2026-07-10. Research background: [music-notation-research.md](music-notation-research.md).*

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

### Phase 1 — Semantic model + MusicXML importer (`draxul-notation`)

- [ ] Add tinyxml2 to `cmake/FetchDependencies.cmake` (pinned tag, static)
- [ ] Model types per sketch above (+ `Fraction` arithmetic: add/compare/reduce)
- [ ] Importer: MusicXML **partwise** documents; score-header (title/composer, part-list); per-measure: `divisions`, `attributes` (key/time/clefs/staves), `note` (pitch/rest/chord/duration/type/dots/staff/voice/tie/accidental/time-modification), `backup`/`forward`
- [ ] Onset computation per voice with backup/forward handling; store on note; validate measure fill vs time signature (log WARN, don't reject — real files are sloppy)
- [ ] Reject `timewise` documents with a clear error (rare; convert later if ever needed)
- [ ] Unit tests (`tests/notation_importer_tests.cpp`, Catch2, fixtures as inline strings or `tests/fixtures/musicxml/`): pitch/alter/octave mapping, divisions normalization incl. mid-file change, chords, two-staff piano with voices + backup, ties, tuplets, dotted rhythms, malformed-input errors
- [ ] Acceptance: importer round-trips the user's sample piece into a model whose note count / measure count / part-staff structure matches the source (spot-checked)

### Phase 2 — Verovio layout engine (`draxul-scoreview`)

- [ ] FetchContent verovio, pinned release tag, `SOURCE_SUBDIR cmake`, built as static lib; disable Humdrum/ABC/PAE importers via its CMake options (verify names); mark headers SYSTEM
- [ ] Bundle Verovio's runtime resource dir (`data/` — SMuFL fonts + metadata): macOS → app bundle `Resources/verovio` (MACOSX_PACKAGE_LOCATION pattern, top CMakeLists ~line 210); Windows → `$<TARGET_FILE_DIR:draxul>/verovio-data` (fonts-copy pattern, ~lines 383-398); resolve at runtime via the existing runtime-path helper
- [ ] `VerovioLayoutEngine : ILayoutEngine` (pimpl; verovio headers only in the .cpp): map `LayoutOptions.page_size_px`/`pixel_scale` → Verovio page width/height/scale units; load MusicXML or `.mxl` bytes; `render_page_svg(n)`
- [ ] Unit test: load a small fixture, assert page_count ≥ 1 and SVG output is non-empty, contains `<use` glyph refs and a staff path
- [ ] Acceptance: engine lays out the sample piece; log INFO timing for load + first layout (baseline number for later)

### Phase 3 — SVG interpreter → ScoreDrawList

- [ ] Path-data parser: `M L H V C Q Z` + relative variants (quadratics promoted to cubics); tolerant of whitespace/comma separators
- [ ] Interpreter over Verovio's SVG dialect: `<defs>/<symbol>/<path>` → `SymbolOutline`; `<use xlink:href>` → `GlyphInstance`; `<path>`/`<polygon>` fills → `FilledPath`; `<line>`/`<rect>`/`<polyline>` strokes → `StrokedLine`; nested `<g transform="translate/scale">` stack; capture `id`/`data-id` per op; viewBox → page coordinate normalization
- [ ] Check in a canned Verovio SVG fixture (generated once from a fixture piece) + tests asserting symbol/glyph/line counts and a few known coordinates — this doubles as the tripwire for SVG-dialect drift when bumping the Verovio pin
- [ ] Acceptance: interpreter consumes every element type Verovio emits for the sample piece with zero "unhandled element" warnings

### Phase 4 — ScoreHost renders the piece

- [ ] Load file from `HostLaunchOptions::source_path` (existing CLI plumbing); parse into model (Phase 1) *and* feed bytes to layout engine (Phase 2)
- [ ] Layout on `set_viewport` (fit page width to viewport, `pixel_scale`-aware); dirty-flag rebuild (layout_dirty / draw_list_dirty, markdown-host pattern)
- [ ] `score_render_nvg`: replay ScoreDrawList through the NanoVG callback — symbols instanced by transform replay, fills, strokes; page background + subtle page edge; only draw pages intersecting the viewport
- [ ] Vertical scroll: wheel/trackpad + PageUp/PageDown/Home/End (crib `markdown_scroll` behavior); zoom via existing `font_increase`/`font_decrease` action names → re-layout at new scale
- [ ] `status_text()`: `"<title> — p. <n>/<m>"`; `default_background()` neutral gray so pages read as pages
- [ ] Update [docs/features.md](../docs/features.md) (new host + CLI) per repo rule
- [ ] Acceptance: **the v1 target** — the user's sample piece renders cleanly (correct clefs/key/time, beams, accidentals, slurs/ties, dynamics as Verovio engraves them), crisp on retina, smooth scroll; `py do.py smoke` + `ctest` green
- [ ] Stretch: `draxul-render-score` snapshot scenario + bless entry, if the render-test harness extends naturally

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

- **Model↔layout ID bridge** — serialize our model to MusicXML/MEI with
  stable IDs so Verovio's element IDs map back to model notes. Prerequisite
  for highlighting and editing. This is the hard seam; design when we get here.
- **Timemap + playback highlight** — Verovio timemap → per-element recolor.
- **Flowing mode** — `breaks: none` single system, horizontal scroll.
- **Editing** — command-based model mutations, hit-testing via element_id,
  re-layout loop; MIDI step input.
- **Glyph atlas optimization** — Bravura through TextService if NanoVG
  replay ever measures too slow.
