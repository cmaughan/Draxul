# Music Notation Rendering — Research Notes

*Researched 2026-07-10. Companion implementation plan: [scoreview.md](scoreview.md).
Upstream authoring/production side (editors, text formats, converters, MusicXML/MNX
status): [music-editors-musicxml-research.md](music-editors-musicxml-research.md).*

Goal: display a clean, interactive, editable view of piano music from a score
file, in a C++ app rendering via Metal/Vulkan (Draxul). End vision: a score
that flows across the screen (iPad/iPhone class UX) with real-time note
highlighting. This note captures the state of the art and the recommended
architecture.

## TL;DR

- **MIDI is the wrong canonical format** — it is a performance recording
  (pitches + timings), not notation. No stems, voices, rests, enharmonic
  spelling, or measures. Use **MusicXML** (or MEI) for interchange, our own
  semantic model internally, and treat MIDI strictly as an import/recording
  source that always needs human cleanup — which the editor provides.
- **Don't write the engraving layout engine from scratch on day one; do write
  the renderer and editor ourselves.** Embed **Verovio** (C++, v6.x in 2026,
  very active) as the layout engine behind an interface, draw its output
  through our own GPU pipeline, and keep the option to replace it with a
  custom piano-scoped layout engine later.
- **Drawing notes cleanly is a solved problem**: SMuFL fonts (Bravura,
  Leland). Same FreeType-atlas + instanced-quad discipline Draxul already
  uses. The hard part of notation is *where things go* (layout), not *what
  they look like*.
- **Editing paradigm: semantic-model-first.** The document is notes/voices/
  durations; layout is a pure function of that model; edits are undoable
  commands. Never let the user drag glyphs freely.

## Formats

| Format | Role | Notes |
|---|---|---|
| MusicXML | Interchange standard | 220+ apps. Verbose XML. Partwise form dominates. MuseScore export is the de facto dialect |
| MEI | Academic/richer | Verovio's native format |
| MNX | Future successor | W3C CG, still pre-1.0 as of mid-2026 (schema version integer 4). Watch, don't build on |
| MIDI | Performance data | Import-only source; requires quantization, voice/hand separation, pitch spelling |
| ABC / LilyPond | Text formats | Compact / beautiful respectively; wrong shape for this app |

## Embeddable native rendering engines

The field of *embeddable native* engines is small. Web has many options
(VexFlow, OSMD, alphaTab — all JS/TS). For C++:

| Engine | License | Assessment |
|---|---|---|
| **Verovio** | C++17, no deps, LGPL-3 | The serious open option. MEI-native; imports MusicXML/ABC/Humdrum; outputs SVG + MIDI + **timemaps**. Active (v6.1, 2026) |
| MuseScore engraving core | C++/Qt, GPL-3 | Best OSS engraving quality but GPL is viral and the code is not decoupled from the app/Qt |
| SeeScore SDK | C/C++, commercial | Turnkey MusicXML rendering for iOS/macOS; watermarked eval, per-app license |
| LilyPond | Batch compiler | Gorgeous, unusable interactively |
| Guido Engine | C++, LGPL | Mostly dormant |

Verovio has features built for exactly the end vision:

- **Timemap export**: JSON mapping ms / quarter-note time → note IDs turning
  on/off — built for playback highlighting.
- `getElementsAtTime()` — query what is sounding at time t.
- `breaks: none` — one endless system → the flowing-score layout.
- Every SVG element carries the source element ID → hit-testing and per-note
  recoloring are addressable.

## UX reference points

- **Dorico** — semantic editing model, engraving gold standard (Finale was
  discontinued in 2024 and endorsed Dorico).
- **StaffPad** — Apple-Pencil handwriting → notation; ceiling for tablet
  authoring (multi-year ML project; out of scope).
- **Soundslice** — best-in-class "score flows and highlights in sync" (custom
  web canvas engine). Closest to the end vision.
- **flowkey / Simply Piano** — the practice-loop interaction (wait for the
  right note, scroll on success).

## Would we write it from scratch? (per layer)

1. **Rendering (GPU drawing)** — yes. Small, our core competence, and owning
   it is what makes flowing/highlighting at 120 Hz possible.
2. **Engraving layout** — not initially. Beam slope quantization, accidental
   stacking, two-voice notehead offsets, cross-staff beaming, slur shaping —
   an endless tail of Elaine Gould rules. Verovio provides a competent version
   today. A custom piano-scoped engine becomes feasible later precisely
   because we can scope it (two staves, ≤2 voices/staff, optionally no line
   breaking in flowing mode).
3. **Data model + editor** — yes, from day one. This is the product. Verovio
   stays a replaceable pure function `model → geometry`.

Verovio integration cost in a Metal/Vulkan app: it emits *constrained,
machine-generated* SVG — each distinct glyph appears once in `<defs>` and is
instanced via `<use>` (maps 1:1 onto atlas instancing), plus lines and Bézier
paths for staves/stems/beams/slurs. A small interpreter for that dialect is
days of work on Draxul's stack. LGPL-3 note: fine for a personal/dev build
statically linked; for distribution, link as an embedded dynamic framework or
keep the module optional.

## Drawing notes cleanly: SMuFL

**SMuFL** (Standard Music Font Layout) is the standard used by Dorico,
MuseScore 4, and Verovio. It maps every notation glyph to codepoints
(U+E000+) and ships per-font JSON metadata: `engravingDefaults` (canonical
staff-line/stem/beam/slur thicknesses in staff spaces) and per-glyph anchors
(e.g. where a stem meets a notehead). Reference fonts **Bravura** (Steinberg)
and **Leland** (MuseScore) are SIL-OFL — free to embed. Sizing convention:
1 em = staff height (4 staff spaces).

Rendering maps directly onto Draxul's pipeline: glyphs as atlas quads or
filled paths; staff lines/stems/barlines/ledger lines as AA'd quads (~1.5 px
weights, same discipline as terminal grid lines); beams as filled
parallelograms; slurs/ties as tapered cubic Béziers drawn as filled outlines
(NanoVG handles these well). No HarfBuzz needed — SMuFL anchors replace
shaping. Per-note highlighting is a per-instance color write.

## Editing paradigm (future phases)

The score is a semantic document; notation is a projection of it. Model =
parts → measures → voices → events (chords/rests with rational durations,
staff assignment for cross-staff piano writing). Layout is derived state
computed in one place. Edits are commands (`InsertNote`, `ChangeDuration`,
`Transpose`) with inverses for undo. Every layout box carries its source
model ID, so one map serves selection hit-testing, playback highlighting, and
tap-to-audition.

Input methods ranked for piano authoring speed:

1. **MIDI keyboard step input** (duration with one hand, pitches/chords on
   the keyboard) — the universally converged pro paradigm.
2. Real-time MIDI record → quantize → edit. Research SOTA:
   MIDI2ScoreTransformer (ISMIR 2024) predicts notation incl. staff
   assignment and stem direction from performance MIDI. Output is ~90% right
   — the editor is the answer to the last 10%.
3. Touch corrections: tap-select, vertical drag with diatonic/chromatic snap,
   long-press palettes. Constrained gestures on the semantic model.

## Flowing score / real-time highlight (future phases)

Keep a time map (score time → note IDs + x positions; Verovio exports this).
Highlight = recolor instances on note-on/off; cursor/scroll = interpolate x
by score time. Design decision to make early: **engraved spacing** (log-ish
duration spacing, variable scroll speed — what Soundslice does; more
readable) vs **time-proportional spacing** (constant scroll speed). Default
to engraved spacing with smooth variable scroll for a practice app.

## Sources

- [Verovio](https://www.verovio.org/index.xhtml) · [reference book — toolkit methods](https://book.verovio.org/toolkit-reference/toolkit-methods.html) · [MIDI playback/highlighting guide](https://book.verovio.org/interactive-notation/playing-midi.html) · [GitHub](https://github.com/rism-digital/verovio)
- [W3C Music Notation CG (MNX status)](https://www.w3.org/community/music-notation/) · [MNX spec draft](https://w3c.github.io/mnx/docs/)
- [MuseScore component reusability discussion](https://github.com/orgs/musescore/discussions/16459)
- [MIDI2ScoreTransformer (ISMIR 2024)](https://arxiv.org/abs/2410.00210) · [code](https://github.com/TimFelixBeyer/MIDI2ScoreTransformer)
- [SeeScore SDK](https://www.seescore.co.uk/developers/seescore-sdk/) · [licensing](https://www.seescore.co.uk/developers/musicxml-sdk/)
- SMuFL: [smufl.org](https://www.smufl.org/) — Bravura/Leland fonts (SIL OFL)
