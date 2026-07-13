# ScoreView milestone 5: the stream (dynamic music + player memory)

The manifesto's heart, specified by the user 2026-07-13: show a short
rolling window of music (~2 bars visible, configurable) and **dynamically
append music to its end**. Understand the piece up front (key, chords and
their neighborings, motifs, rhythm figures). Then, as the runner rolls,
**write music** — subtly melodic, in the piece's own language — that
repeats the phrases, components, notes, and *timings* the player is
demonstrably struggling with. Persist everything between sessions (JSON):
notes succeeded, notes missed, chords in trouble, and — very important —
timing tendencies (e.g. triplets consistently played slow must be
detected, drilled, and fixed). The stream converges on the actual piece
— beginning, middle, end, sliced and rearranged — **only as the player
improves**: convergence is gated on demonstrated mastery, never on
elapsed time. How long that takes varies per player and per piece; a
session (or many) may never reach the full performance, and that's
correct behavior — the problems get drilled until they're consistently
right, and *getting* to the whole piece is the goal, not the schedule.

## Architecture at a glance

```
                     ┌────────────────────────────────────────────┐
  source piece ──►   │ PieceProfile (S1)                          │
                     │  key estimate · chord inventory+transitions │
                     │  motif n-grams · rhythm figures · hand split │
                     └──────────────┬─────────────────────────────┘
                                    │
  progress JSON ──►  PlayerModel (S0)◄──── roll verdicts + timing deltas
   (persisted)                      │
                                    ▼
                     StreamComposer (S3/S4): picks/fabricates the next
                     bar(s) → model measures with per-note provenance
                                    │  minimal MusicXML writer
                                    ▼
                     Rolling window document (S2): drop head bar,
                     append tail bar, re-engrave via Verovio,
                     re-join timemap, re-base qstamps, carry verdicts
                                    │
                                    ▼
                     existing conveyor/roll pipeline (unchanged seams)
```

## S0 — Player memory: timing capture + the progress store ✅ (2026-07-13)

The foundation; valuable before any generation exists (today's full-piece
roll already produces the data).

- **Timing capture in Roll**: on every hit record `delta_q = position_q −
  onset_q` (negative = early), in beats so it's tempo-independent. Add a
  timing-quality weight to the accuracy sample (center of window = 1.0,
  edges taper) so "hit but badly late" stops counting as perfect — this
  is what lets the tempo controller and the drills *feel* timing.
- **Verdict provenance**: every judged note maps to a source reference —
  for now `piece qstamp + element id`; once generation exists, drills
  carry `(kind, source_ref)` provenance so a missed drill note still
  debits the right piece location/skill.
- **`PlayerModel`** (in-memory aggregates, pure + unit-testable):
  per-pitch hit/miss/wrong-near counts; per-piece-onset attempts/hits/
  timing mean+variance; per-chord (sorted pitch multiset) hit/miss/split
  counts (split = chord notes judged apart in time); per-rhythm-figure
  timing drift (see S1 figures); per-bar mastery score in [0,1] (decayed
  blend of accuracy × timing quality); session records; best/last tempo.
- **Persistence**: versioned JSON, one file per piece keyed by source
  content hash: `<user-data>/scoreview/progress/<hash>.json` (same root
  the config machinery already resolves; atomic tmp+rename writes).
  Flushed at bar boundaries and on shutdown; loaded at startup — the
  session resumes at a tempo informed by history, and the generator
  knows day-one what needed work yesterday.
Shipped: `NoteOutcome`/`ChordOutcome` streams from the FlowController
(hit delta + center-weighted quality feeding the accuracy EMA;
edge-quality floor `kRollEdgeQuality`; chord clean/split/miss at window
close with `kChordSplitQ`), `PlayerModel` aggregates (Welford timing,
per-onset recent-encounter rings, near-miss stray attribution, bar
mastery = recent-mean), `progress_store` (FNV-1a piece hash, tmp+rename
atomic writes), host wiring (load at open, resume tempo from last
session, flush per bar + session end). Verified live: two consecutive
runs — save then load then extend — with the Grieg's real LH chords in
the file. Figure-level drift lands with S1's rhythm figures.

- **Schema sketch** (all fields versioned, unknown fields preserved):

  ```json
  {
    "version": 1,
    "piece": { "title": "Walz", "hash": "…", "marking_qpm": 130 },
    "tempo": { "best_frac": 0.82, "last_frac": 0.74 },
    "sessions": [ { "start": "…", "seconds": 1840, "notes": 612 } ],
    "pitch": { "60": { "hit": 41, "miss": 3, "wrong_near": 2,
                        "dt_mean_q": -0.06, "dt_var_q": 0.01 } },
    "onset": { "12.5": { "hit": 4, "miss": 2, "dt_mean_q": 0.11 } },
    "chord": { "48+64+67": { "hit": 4, "miss": 6, "split": 3 } },
    "figure": { "triplet_8": { "seen": 12, "drift_q": -0.18 } },
    "bar_mastery": { "1": 0.9, "2": 0.4 }
  }
  ```

## S1 — Piece analysis (`PieceProfile`, computed at load)

All from the semantic model + timemap; pure, unit-tested against the
Grieg (known answers) and synthetic fixtures.

- **Key estimate**: Krumhansl–Schmuckler correlation of the pitch-class
  histogram against major/minor profiles, with the notated signature
  (fifths) as a prior; windowed per 8 bars to catch modulations. Output:
  global key + per-section keys with confidence.
- **Chord inventory + "nearings"**: cluster near-simultaneous pitches at
  each onset into vertical sonorities; classify by pitch-class set
  (triads/sevenths, inversion); record each chord's *contexts* — the
  chords before and after it (a bigram transition table), its bar
  positions, and its hand split. This is the generator's harmonic
  vocabulary AND its join table (what legally follows what).
- **Motif mining**: n-grams (3–8 notes) over the top voice by interval +
  rhythm shape; keep recurring ones with counts and locations. The LH
  accompaniment figure (the waltz oom-pah-pah) is detected the same way
  on the bottom staff — it becomes the default accompaniment template.
- **Rhythm figures**: the piece's distinct duration patterns per beat
  span (dotted pairs, triplets, straight eighths…), each with its
  canonical grid — the *timing drill targets* that S0's drift statistics
  key on.

## S2 — The rolling window (display + append machinery)

Replaces "engrave the whole piece once" with a window that can change
while playing — the enabling mechanics for everything else.

- **Window document**: `visible_bars` (config `[score] window_bars`,
  default 2) + `lookahead_bars` (default 6) model measures. A minimal
  **MusicXML writer** for our generated vocabulary (notes, chords,
  rests, ties, dots, tuplets, key/time/clefs) feeds Verovio — the exact
  inverse of the importer subset, golden-file round-trip tested
  (write → Verovio load → timemap/pitches match the model).
- **Swap at bar boundaries**: when the playhead crosses into bar k+1,
  build the next window (drop head, append tail), engrave, re-join the
  timemap, and **re-base**: window-local qstamps ↔ stream qstamps via a
  running offset; transport position, verdict states of still-visible
  notes, lit set, and metronome bar phase all carry over. Departed bars'
  verdicts flush into the PlayerModel as they scroll off.
- **Continuity rules**: never re-engrave content at or left of the
  playhead; swap only affects the tail. Engraving ~8 bars is fast; do it
  synchronously at the boundary first, move to a worker double-buffer
  only if a frame hitch is measurable (log the engrave time).
- **Proof**: passthrough mode — the window walks the actual piece
  unchanged. Acceptance: a full Grieg roll via the window produces the
  same onset sequence and verdicts as the monolithic strip (log/diff
  verification), with screenshots across several swaps showing no visual
  discontinuity at the playhead.

## S3 — The composer (generation v1)

`StreamComposer`: given PieceProfile + PlayerModel + transport state,
emit the next bar as model measures with per-note provenance.

- **Sources on a mastery ladder** (weakest-first, spaced-repetition
  style): (a) *piece bars* at the current frontier; (b) *hands-separate*
  or *simplified* piece bars (strip inner voices/ornaments) when a bar's
  mastery is low; (c) *fabricated drills* for the weakest items:
  - chord-pair alternation for fumbled transitions (from the bigram
    table, so it's the piece's own progression);
  - rhythm drills: a struggling figure (e.g. those slow triplets) first
    on one repeated pitch, then on the motif that carries it;
  - motif repetition with subtle in-key variation (transpose within the
    key, neighbor tones, step-preferring contour, chord tones on strong
    beats) — the "slightly melodic" requirement;
  - problem-note spotlights: the missed pitch embedded in short scale/
    arpeggio fragments from the piece's key.
- **Joins**: bars connect through the transition table (end a drill on a
  sonority that legally leads to the next bar's opening); keep the LH
  template running underneath RH drills so everything sounds like the
  piece, not like an exercise book.
- **Anti-boredom constraints**: never the same drill more than twice in
  a row; interleave a mastered "victory lap" bar regularly; drill
  probability ∝ weakness but capped.
- **Every generated note carries provenance** so its verdict feeds the
  right statistic — the loop that makes the lesson adaptive.

## S4 — Convergence and slicing (mastery-gated, never time-gated)

- The **frontier roams**: practice segments picked from the beginning,
  middle, and end of the piece (weighted by mastery), so the piece is
  learned as slices that grow and merge — the user's "from the end, from
  the beginning, from the middle, sliced up".
- **Promotion is earned, not scheduled**: drill share decays, real-piece
  runs lengthen, and the tempo target climbs only when the mastery map
  crosses explicit thresholds (accuracy AND timing consistency per
  bar/figure, sustained over repeated encounters — not a lucky pass).
  Nothing promotes because time passed; a struggling player gets more
  drilling, not the piece anyway. The full start-to-finish performance
  happens only when every slice has earned it — that run IS the
  milestone's acceptance test, whenever it comes.
- Session pacing: fatigue guard (drill density eases late in a long
  session), and the JSON's session records let tomorrow start exactly
  where mastery says, not where the clock left off.

## Verification instruments

- **Weakness-profiled bot**: extend the bot family with per-pitch/figure
  error and timing-skew profiles (deterministic seed). Acceptance: a bot
  that rushes triplets and fumbles one chord must, within N bars, be
  served triplet drills and that chord's alternation drill (log lines:
  `stream: drill <kind> reason <stat>`), and its stats must visibly
  improve the mastery numbers when the profile is corrected.
- Golden-file tests for the MusicXML writer and the JSON schema
  (round-trip, version migration, unknown-field preservation).
- Long-run soak: bot plays 30+ minutes through the window; no drift
  between transport, metronome bar phase, and engraved bars (assert via
  periodic log sampling).

## Order & risks

S0 → S1 → S2 → S3 → S4; each lands green before the next starts (the
established per-phase commit pattern). Risks called out up front:
re-engrave spacing shifts right of the playhead (accepted; anchored at
the playhead), repeats/non-monotonic onsets (window sidesteps the old
clamp since content is linearized by construction), tuplet fidelity in
the MusicXML writer (Verovio is strict about tuplet encoding — golden
tests first), and progress-file growth (cap session history, aggregate
forever-stats).
