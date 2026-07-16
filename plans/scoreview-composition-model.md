# ScoreView composition model — decision note

**Date:** 2026-07-16 · **From:** kanban `22 scoreview-module-boundaries`, item 3
**Question:** do composers keep fabricating MusicXML strings, or compose
against `notation::ScoreDocument` measures serialized through a writer?

## Decision

**Semantic composition is the destination, adopted together with per-note
provenance; string fabrication stays until then, guarded by the golden-XML
corpus.** Concretely:

- Today: `IComposer` implementations emit fabricated bars as MusicXML
  `<measure>` strings through the shared `measure_xml` writer
  (draxul-score-learn). This path is proven — the composer tests engrave
  drill bars through Verovio and assert their onsets pixel-true — and the
  golden-XML tests (tests/scoreview_measure_xml_tests.cpp) pin the emission.
- When S3-polish work lands per-note provenance ("model measures with
  per-note provenance", plans/scoreview-stream.md), composers switch to
  building `notation::ScoreDocument` measures; a measure-level serializer
  (grown from `measure_xml`'s emission helpers) turns them into the
  MusicXML that `SourceSlicer::window_xml_for` already accepts. Source bars
  keep slicing VERBATIM — the semantic model is the fabrication currency,
  never a re-encoding of the original piece (that fidelity trick stays).

## Why semantic wins (eventually)

The model-expressiveness audit found **no gaps for drill content**:
`notation::Note` already carries staff, voice, `chord_with_prev`, grace,
ties, accidentals, exact `Fraction` durations, and `TimeModification`
(tuplets) — tuplets being exactly the fidelity risk the stream plan flags
for hand-built XML (Verovio is strict about tuplet encoding). Per-note
provenance naturally annotates model notes; annotating substrings of an XML
blob does not survive contact with a second composer implementation.

## Why not now

- The missing piece is the serializer (ScoreDocument → MusicXML with
  per-voice backup/forward). Writing it before any composer needs tuplets
  or per-note provenance is speculative machinery.
- The string path is small, tested end-to-end through Verovio, and shared
  (one writer, N composers) since kanban 20 phase 3.

## Consequences

- `StreamBarPlan::drill_xml` keeps carrying the fabricated `<measure>`
  string for now; the field is replaced (not augmented) by model measures
  when the serializer lands.
- On adoption, `draxul-notation` becomes a dependency of
  `draxul-score-learn`, and the serializer lives beside `measure_xml` so
  every composer shares one tested emission path.
- Until then, any new fabricated figure (melodic/motif drills, rhythm
  figures — the S3 polish list) must extend the golden-XML corpus in the
  same change.

Re-litigating this per composer is the failure mode this note exists to
prevent: composer #2 uses the string writer unless it needs tuplets or
per-note provenance, in which case it triggers the serializer work above.
