# ScoreView — The Science-Backed Composer (game plan)

How the stream composer should teach a piece, derived from
[scoreview-learning-research.md](scoreview-learning-research.md) (two verified research
passes) and measured against the composer as it stands today
(`modules/score/draxul-score-learn/`). Extends milestone 5
([scoreview-stream.md](scoreview-stream.md), S0–S4 shipped).

Evidence tiers carried over from the research note: **[V]** adversarially verified,
**[P]** primary-source quoted, **[?]** directional.

---

## Part 1 — The top 10 features of an optimal piano-teaching app

Ranked by (evidence strength × design leverage). Each is a *product* commitment, not an
implementation detail.

1. **Chunk on musical structure, never on fixed windows.** Practice units must be
   phrases/sections, because expert practice starts and stops at section boundaries
   (p<.001) and the piece's formal hierarchy *is* the memory-retrieval scheme (structural
   position predicted recall 2 years later, R²=.76). **[V]** — *the single highest-leverage
   feature. Shipped in C0/C1: arcs now aim at detected phrases.*
2. **Accuracy gates tempo — always.** Tempo may only rise off *clean complete* passes. The
   strongest predictor of next-day retention is the % of complete run-throughs played
   correctly (r=−.71); practising below target tempo verifiably **doubles** ceiling speed
   (500 ms/keystroke → 2× max, held ~2 months). Speed is an *output* of accurate slow reps. **[V]**
3. **Errors must trigger immediate re-serve, never be played past.** More incorrect trials
   in practice predicted *worse* retention (r=+.48). A run containing an uncorrected error
   must count against mastery, and the fumbled fragment must come back *now*, corrected. **[V]**
4. **Weight the tails and seams, not the openings.** Within a section, recall runs ~.97 at
   the first bar and collapses to ~.28 by serial positions 5–8; hesitations concentrate at
   section ends and cue points. Section openings are cheap anchors; interiors and the seams
   between chunks are where the time should go. **[V]**
5. **Space reviews across days with expanding intervals.** Optimal gap grows with the
   retention horizon; minute-scale gaps do *nothing* (no spacing effect at 1–15 min in piano
   novices — no forgetting occurred, so nothing was reconstructed). Spacing is a
   *calendar* feature, not a slot feature. **[V]**
6. **Treat sleep as a stage of learning.** Overnight yields +17.7–28.9% speed with zero
   practice (vs +0.9% for equal waking time) and *selectively* fixes the hardest transition
   (+17.8% vs +1.4% for easy ones). The app should expect problem points to improve
   overnight — re-test them, don't grind them. **[V]**
7. **Interleave across chunks; correct-repeat within a hard one.** Interleaving's retention
   benefit is large in lab tasks (SMD≈0.92) but negligible in applied ones (≈0.23, n.s.),
   and the music studies contradict each other. Rotate among chunks (that much is safe);
   don't randomize inside a passage the player is still getting wrong. **[V]**
8. **Hands-separate is a load-reducer, never mastery evidence.** Motor learning is
   effector-specific — piano speed gains transfer within a hand but **not** across
   (F(1,10)=0.3, p=.60), and two-hand sequences are stored as an integrated whole that
   one-hand practice transfers to only ~10%. Promotion must require hands-together. **[V/P]**
9. **Show the target before the attempt.** Hearing a correct model of the passage *before*
   practising improved gains both during training *and* across the overnight interval — a
   cheap, high-leverage move. **[P]**
10. **Feedback: visual, target-relative, and faded.** Visual discrepancy-vs-expert feedback
    refined technique where audio-only did not. But feedback must add information the player
    can't already perceive, and constant feedback risks becoming a crutch — fade it as
    mastery grows. **[P + flagged gap]**

### Deliberate non-goals (evidence says don't)
- **No backward chaining.** The one direct keyboard test found it no faster and
  *significantly more error-prone* after a week (ηp²=.23–.25); it distorts the natural serial
  order. Our forward frontier is already correct — keep it, and don't let anyone "improve" it. **[P]**
- **Never optimize for in-session fluency.** Acquisition ≠ retention; the schedule that looks
  best today is not the one that builds skill. **[V]**
- **Resist the fluency trap.** Players *prefer* easy blocked repetition that retains poorly. **[P]**

---

## Part 2 — Where the composer stands today (honest scorecard)

**Already right** (keep, and now evidence-backed rather than intuition):
- Mastery-gated, never time-gated arc — matches the whole evidence base.
- Difficulty-first: the arc loops the *weakest phrase* (C1; a fixed slice only as a
  fallback); drills are fabricated from the player's exact troubled voicings. Corroborated by sleep's selective consolidation of
  problem points.
- Forward frontier — accidentally the evidence-backed choice (see non-goals).
- Mild interleaving via `kMinPieceBarsBetweenSpecials` + the rotating special chain.
- Hands-separate as a *rung* (`kHandsSeparateMastery`), then back to the full bar.
- Recent-encounter mastery ring (`kRecentEncounters=8`) rather than a lifetime average —
  "consistency over recent encounters" is the right shape.
- Guidance-keyboard fade (`1 - trailing_clean/3`) already anticipates feedback fading (#10).
- `kDrillOnsetSentinel` keeps fabricated drills out of bar mastery — correct hygiene.

**Gaps, worst first:**
| # | Gap | Evidence violated |
|---|-----|-------------------|
| ~~G1~~ | ~~**No phrase/section/cadence structure exists**; `kSliceBars = 8` slices fixed windows.~~ **CLOSED (C0/C1)** — `PieceProfile` carries phrases/sections with a confidence, and arcs slice on the weakest phrase. | Feature 1 **[V]** |
| G2 | **Spacing is slot-based within a session** (`kDrillCooldownSlots`, `last_review_slot_`). | Feature 5 **[V]** — minute-scale spacing does nothing; must be day-scale |
| G3 | **No sleep-awareness**, despite `Session.start_iso` already being recorded. | Feature 6 **[V]** |
| ~~G4~~ | ~~**No serial-position awareness.**~~ **CLOSED (C2)** — `bar_tail_fraction` weights reviews toward phrase tails, and a seam special serves the join. | Feature 4 **[V]** |
| G5 | **`kPromotionMastery = 0.7` is a mean**, not a clean-complete-pass gate (0.7 mean = 30% wrong is "mastered"). | Features 2, 3 **[V]** |
| G6 | **Per-hand mastery isn't gated** — `bar_mastery` is combined; `hands_done_` is a one-shot bool; hand split is a pitch heuristic (`kHandSplitMidi=60`) not staff assignment. | Feature 8 **[V/P]** |
| G7 | **No pre-chunk target model** — audition voices notes *as* the playhead crosses, not before the attempt. | Feature 9 **[P]** |
| G8 | Composer is **off by default** (`composer_enabled_ = false`). | — (ship gate) |

---

## Part 3 — The game plan (phases C0–C5)

Ordered by dependency and leverage. C0 is the foundation: features 1 and 4 are *impossible*
without it, and it retires the worst gap.

**Status:** C0, C1 and C2 shipped 2026-07-16 (kanban item
`73 scoreview-composer-structure -feature.md`). G1 and G4 are closed — the composer chunks on
detected phrases and weights serial position. C3–C6 remain.

**Restatement detection (2026-07-17):** `PieceProfile::Phrase` now carries `repeat_of`/
`transposed` — phrases whose top-voice interval+rhythm shape already appeared are marked as
restatements (the Grieg's bars 47–50 correctly chain back to bars 8–11). Follow-up for C3+:
the composer should exploit this — a repeated phrase is not new material, so mastery evidence
may transfer between statements and the frontier need not grind both. Motifs likewise now
record every occurrence (`occurrences_q`), not just the first.

**Validation surface (2026-07-17):** the paged reading view now has a green **analysis
overlay** (inspector "Analysis overlay" checkbox / `analysis` launch token) drawing the
detected phrases, key regions, sections and motifs over the score — the eyeball-the-detector
loop for tuning C0's boundaries against real music. First finding it surfaced: the same piece
analyzes slightly differently from `.mxl` vs `.xml` sources (18 vs 16 phrases on the Grieg),
worth a look during detector tuning.

### C0 — Phrase/section structure in `PieceProfile` ✅ *(unlocks #1, #4; fixes G1, G4)*
Add a nested structural layer to the analysis pass. **We already have most of the raw
signals** — this is mostly synthesis, not new DSP:
- **Cadence detection** from the existing chord inventory + "nearings" join table (V–I,
  half, deceptive) — the strongest phrase-boundary cue we already compute.
- **Rests / long-note gaps** straight off the onset axis (the analysis input).
- **Repeat signs / double barlines** from MusicXML (structural boundaries, free).
- **Motif recurrence** (already mined) — a returning motif marks a section start.
- **`key_sections`** (already computed) — a modulation is a strong section boundary.

Emit: `struct Phrase { int start_bar, end_bar; double start_q, end_q; double confidence; }`
and `struct Section { ... std::vector<int> phrase_ids; }`, plus a per-bar lookup giving
`(section_id, phrase_id, serial_position_in_phrase, is_anchor)`.

*Verification:* hand-label phrase boundaries on the Grieg fixture; assert detected boundaries
land on the labelled bars (allow ±1 bar), and that confidence degrades gracefully on material
with no clear cadences.

### C1 — Slice on structure, not `kSliceBars` ✅ *(feature #1; fixes G1)*
- Replace the sliding fixed-8-bar weakest-slice with the **weakest phrase** (or section when
  phrases are short).
- Keep `kSliceBars` only as a **fallback** for low-confidence structure (atonal/contemporary
  material — where the research says chunks are hand-shapes, not tonal groupings anyway **[?]**).
- Anchor every arc on a section-opening bar (the privileged retrieval cue, ~1010 ms
  recognition **[V]**).

### C2 — Serial-position + seam weighting ✅ *(feature #4; fixes G4)*
- Weight review/drill priority by position-in-phrase: tails get more scheduled repetition
  than heads.
- New special: **seam drills** — the last bar of phrase N joined to the first of N+1. This is
  where hesitations provably concentrate, and no current special targets it.

### C3 — Clean-complete promotion + explicit tempo ladder *(features #2, #3; fixes G5)*
- **Promotion gate:** K consecutive **clean complete** passes of the chunk, replacing the
  `≥0.7` mean. (Mean-based mastery lets a chronically fumbled note ride along.)
- **Tempo ladder per chunk:** start submaximal, ramp *only* on clean passes, drop on error.
  Roll already eases tempo per-note; make it chunk-scoped and explicit so "the gate" and
  "the tempo" are one mechanism.
- **Error → immediate re-serve.** *Design tension, resolved:* the product rule is "never
  stops-and-waits" (Guitar Hero for piano) but the research says don't play past errors. The
  reconciliation is **re-serve, not stop** — the stream keeps flowing, and the fumbled
  fragment is scheduled to return immediately rather than the transport halting.

### C4 — Time-aware scheduling *(features #5, #6; fixes G2, G3)* — **the biggest scale error today**
- `PlayerModel`: per-bar/phrase `last_seen_iso` + `review_due`, with **expanding** intervals
  (day-scale: ~1d, 3d, 7d…), scaled to the retention horizon.
- Composer: schedule reviews by **due date across sessions**, not slot cooldowns. Slot
  cooldowns survive only as an anti-boredom spacer *within* a session.
- **Sleep-aware opening:** on the first session after an overnight gap, **re-test** the prior
  session's problem points early (expect them to have consolidated) instead of drilling them;
  only drill the ones that didn't improve. This turns a verified neuroscience result into a
  concrete scheduling rule and is a genuinely novel product moment ("the hard bar got easier
  overnight — here it is").
- **Purity note:** the composer is documented as *pure and deterministic given its inputs*.
  Keep it that way — inject `now_iso` as an input (via `configure` or the program), never
  call the clock inside the composer.

### C5 — Hands, model, feedback *(features #8, #9, #10; fixes G6, G7)*
- **Per-hand mastery** tracked and gated separately; **hands-together required to promote**
  (hands-separate must never satisfy the gate). Replace the `kHandSplitMidi=60` pitch
  heuristic with **staff assignment** from the layout engine (MEI carries staff numbers) —
  the current split is acknowledged as fuzzy and mis-assigns crossing hands.
- **Pre-chunk target model:** audition the upcoming chunk before the attempt (10 reps at
  fixed tempo is what the study used; one clean pass is the cheap version).
- **Feedback fading policy:** generalize the guidance keyboard's `1 - trailing_clean/3` fade
  into a single policy the waterfall and other cues also honour.

### C6 (gate) — turn the composer on by default *(G8)*
Once C0–C3 land, the composer is defensibly better than scrolling the raw piece. Flip
`composer_enabled_` and keep `nocomposer` as the escape hatch.

---

## Sequencing rationale
C0 → C1 → C2 is one coherent thread (structure, then use it, then weight it) and delivers the
strongest-evidence feature first. C3 is independent and could land in parallel — it's the
cheapest big win (a gate change plus a ladder). C4 is the highest-*novelty* work and depends
on nothing but a clock input, but it only pays off across real multi-day use, so it wants the
others in place first. C5 is polish with real evidence behind it.

## Open questions (deliberately not designed here)
- **Dosage:** how many reps/session, and how steeply chunk difficulty should scale scheduled
  repetition. The literature has no numbers — **instrument ScoreView and find out** (we have
  the player model to do it).
- **Feedback scheduling / guidance hypothesis** and **focus of attention** (external vs
  internal cueing): flagged research gaps; a third research pass could constrain #10 properly.
- **Ecological validity:** nearly all the verified motor-learning effects are single-session
  lab finger-tapping tasks; the only real-piece study is N=1 (Chaffin). Our own telemetry is
  the best available check on whether these transfer to weeks-long piece learning.
