# Make the ScoreView composer teach on musical structure, not fixed windows

**Type:** feature
**Priority:** 73
**Raised by:** Claude (Opus 4.8), learning-science research synthesis 2026-07-16

## Goal

Bring the stream composer in line with the verified learning-science evidence in
[plans/scoreview-learning-research.md](../../plans/scoreview-learning-research.md);
the full rationale and phase breakdown live in
[plans/scoreview-composer.md](../../plans/scoreview-composer.md).

The composer's single biggest flaw: `StreamComposer::kSliceBars = 8` slices the piece
into **fixed 8-bar windows**, which is exactly the arbitrary-window chunking the
strongest-verified segmentation evidence forbids. Expert practice starts and stops at
section/phrase boundaries (p<.001), and the piece's formal hierarchy *is* the memory
retrieval scheme (structural position predicted recall 2 years later, R²=.76). It cannot
do better today because `PieceProfile` has **no phrase, section, or cadence structure at
all** — only key, chords, motifs and rhythm figures.

Second finding the evidence forces: within a section, recall runs ~.97 at the first bar
and collapses to ~.28 by serial positions 5–8, and hesitations concentrate at section
ends and seams. Section openings are cheap anchors; tails and seams are where the
practice time belongs. The composer has no notion of position-within-phrase, so it cannot
weight them.

## Design decisions (up front)

- **Phrases are the practice chunk; sections are a coarse grouping.** Chaffin's
  "practice segments start at section boundaries" maps onto the unit a composer actually
  serves. Phrases carry the load; sections exist only where we have real evidence for
  them (key changes), never fabricated.
- **Detect structure from signals the analysis already computes** — cadences from the
  existing chord inventory + "nearings" join table, rests/long-note gaps off the onset
  axis, motif recurrence, and `key_sections`. No new DSP, no new inputs.
- **`analyze_piece` stays pure.** Structure detection is a function of the same
  (onsets, quarters_per_bar, notated_fifths) inputs — analysis and gameplay can never
  disagree about what the piece contains.
- **Confidence gates the fallback.** `PieceProfile::structure_confidence` reports how
  much *real evidence* (not the hypermetric prior) drove the boundaries. Below threshold
  the composer keeps the `kSliceBars` sliding window — which is also the right call for
  atonal/contemporary material, where the research says chunks are hand-shapes rather
  than tonal groupings.
- **Repeat/double barlines are out of scope**: `analyze_piece` takes only onsets, not
  MusicXML, so barline structure would need a new input. Noted as a future signal.
- **Out of scope** (later phases of the same plan): clean-complete promotion gates and
  the tempo ladder (C3), day-scale spaced repetition and sleep-awareness (C4), per-hand
  gating and the pre-chunk target model (C5), enabling the composer by default (C6).

## Phase C0 — phrase/section structure in `PieceProfile`

- [x] Add `Phrase`, `Section`, `BarPosition` to `piece_analysis.h`, plus
      `phrases`, `sections`, `bar_positions`, `structure_confidence` on `PieceProfile`.
- [x] Score phrase-start candidates per bar from: gap/rest before the barline, cadence
      in the preceding bar (V→I authentic, half cadence on V), key change, motif restart,
      and a weak hypermetric prior (2/4/8 bars).
- [x] Accept boundaries greedily with min/max phrase length; derive `bar_positions`
      (phrase id, serial position, phrase length, anchor flags).
- [x] Group phrases into sections at key changes; single section when the key never moves.
- [x] Report `structure_confidence` from evidence only (prior excluded).
- [x] Serialize phrases/sections into the analysis JSON dump.

## Phase C1 — slice on structure, not `kSliceBars`

- [x] `begin_next_arc()` picks the weakest **phrase** (mean bar mastery) instead of the
      sliding fixed 8-bar window.
- [x] Keep `kSliceBars` as the documented fallback when structure confidence is low or
      no phrases were detected.
- [x] Fix the arc `reason` string, which currently hardcodes the `kSliceBars` geometry.

## Phase C2 — serial-position weighting + seam drills

- [x] `try_review` prioritises by position-in-phrase (tails over heads) rather than
      lowest mastery alone.
- [x] New `try_seam` special: serve the last bar of phrase N and the first bar of N+1
      back-to-back as two Review slots — the join is where hesitations provably
      concentrate, and nothing targets it today. Cooldown per seam; joins the rotating
      special chain.

## Tests and acceptance

- [x] Synthetic fixtures with known phrasing (clear 4-bar phrases separated by rests /
      cadences) produce the expected boundaries; unphrased material reports low
      confidence and does not invent structure.
- [x] Real Grieg fixture: detected phrase boundaries land on musically sane bars and
      structure confidence clears the composer's threshold.
- [x] Composer arcs align to phrase boundaries when structure is confident, and fall back
      to the `kSliceBars` window when it is not.
- [x] Review selection prefers phrase tails over heads at equal mastery.
- [x] Seam special serves the two boundary bars adjacently, honours its cooldown, and
      does not starve the other specials in the rotation.
- [x] `ctest -R scoreview` green; `cmake --build build --target draxul draxul-tests`
      plus `py do.py smoke` before commit.

## Result (shipped 2026-07-16)

C0/C1/C2 landed; `docs/features.md` and `plans/scoreview-composer.md` updated. Full suite
green (1642 passed / 0 failed, 144 scoreview cases) and `py do.py smoke` clean.

**Two design refinements the tests forced** — both tighten the honesty property, and both
are worth knowing before touching the detector:

1. **The hypermetric prior no longer accepts boundaries; it only breaks ties.** As first
   written, evidence and the prior were summed against one threshold, so a 0.25 prior plus
   any faint cue (≥0.30) manufactured a phrase. Acceptance now tests EVIDENCE alone, and
   the prior survives only to choose where an over-long phrase gets cut. Thresholds are
   set so no single weak cue (motif restart, half cadence) can open a phrase, while a real
   cadence, a modulation, or genuine breathing space each stand alone.
2. **A forced (max-length) cut carries zero confidence.** It is an admission that we had
   to chunk somewhere, not a boundary we detected — previously such a cut inherited
   whatever faint evidence sat at that bar (a motif restart at 0.45) and inflated
   `structure_confidence` past the composer's 0.35 gate on material with no phrasing at
   all.

**Test-fixture lesson worth keeping:** a "cue-free" fixture is harder to build than it
looks. Random pitches — chromatic *or* diatonic — make the windowed key estimate waver,
and a wavering estimate reports a modulation, which is itself a boundary cue; the fixtures
therefore use a fixed pitch cycle keyed to the absolute beat, with a period that divides
both the 8-bar key window and its 4-bar hop, and assert `key_sections.empty()` as a
precondition so a fixture that grows a cue fails loudly instead of quietly passing.

**Not done here** (later phases of [plans/scoreview-composer.md](../../plans/scoreview-composer.md)):
C3 clean-complete promotion + tempo ladder, C4 day-scale spacing + sleep-awareness,
C5 per-hand gating + pre-chunk model, C6 composer on by default.
