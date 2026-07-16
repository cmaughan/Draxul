# SourceSlicer corpus equivalence

**Type:** test
**Priority:** 19
**Raised by:** Claude

## Gap

The Grieg mid-piece and clef-return regressions are strong, but one score cannot cover the range of MusicXML attribute, voice, repeat, pickup, meter, key, clef, transposition, grace, tuplet, and multi-staff transitions that a random rolling window may cross.

## Implementation plan

- [ ] Build a small license-safe corpus of hand-authored/committed MusicXML fixtures covering the supported vocabulary and known boundary transitions.
- [ ] Extract a reusable comparison helper that engraves the monolith and a sliced window, normalizes qstamps within tolerance, and compares sounding pitches plus relevant notation/attribute state.
- [ ] Generate seeded random `(first_bar, count)` windows for every corpus piece, including first/last/one-bar/out-of-range-clamped boundaries.
- [ ] Compare SourceSlicer bar timing/indexing against the semantic importer where both support the same document.
- [ ] Minimize failing seeds to a concrete fixture/window and print the source, seed, and bar range.
- [ ] Gate expensive Verovio corpus passes with the existing slow-test mechanism while retaining a small always-on subset.

## Tests and acceptance

- [ ] Every source onset inside a window appears at the shifted qstamp with identical pitches and no extra onsets.
- [ ] Active key/time/clef/divisions/staff state at each window head matches the monolith.
- [ ] Malformed/unsupported constructs fail with an explicit error rather than silently generating a different score.
- [ ] The Grieg measure-53 case remains as a named regression inside the broader corpus.

## Dependencies and parallelism

Independent core test task. It should land before practice-loop and manual-hand features extend slicing policy, and serves as a gate for ScoreHost decomposition without touching the host.

<model>GPT-5 Codex</model>
