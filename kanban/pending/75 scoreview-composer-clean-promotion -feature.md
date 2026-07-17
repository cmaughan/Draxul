# ScoreView composer C3: clean-complete promotion, error re-serve, tempo ladder

**Type:** feature
**Priority:** 75
**Raised by:** Claude (Opus 4.8), from plans/scoreview-composer.md phase C3

## Goal

Close G5: `kPromotionMastery = 0.7` is a MEAN over recent onset qualities, so a bar
that is 30% wrong forever still "promotes". The strongest retention predictor in the
evidence base is the percentage of COMPLETE CORRECT passes (Duke/Simmons r = -.71),
errors must trigger re-serve rather than being played past, and tempo should rise
only off clean passes (submaximal practice verifiably raises ceiling speed).

## Design decisions

- A **pass** = one traversal of a bar, detected by bar transition in the outcome
  stream (outcomes arrive in transport order; each stream slot is a whole bar, so
  interleaved review/drill slots close passes correctly). A pass is **clean** iff
  every outcome in it was Correct and no stray landed in it.
- **Promotion** = `kPromotionCleanPasses` consecutive clean passes, replacing the
  mean gate. Mean mastery survives as triage (what to practice next); clean passes
  decide DONE.
- **Re-serve is bounded by the append-only program**: a fumbled bar returns at the
  next PLANNED slot, which plays after the ~14-bar engrave lookahead. True
  mid-window injection is the rewriting-composer future recorded on kanban 20 —
  do not force it here.
- **Tempo ladder** is per-bar state in the player model (persisted): clean passes
  raise the bar's ceiling fraction, dirty passes lower it; the host applies the
  ladder as a CAP on the roll tempo when the playhead enters the bar. The existing
  per-note easing still handles micro-adjustment; the tempo lock always wins.

## Phase a — clean passes + promotion + re-serve

- [x] PlayerModel: per-bar pass ring (clean/dirty, recent window) + trailing
      consecutive-clean count; strays dirty the open pass; drills never touch it;
      serialize round-trip with unknown-field preservation.
- [x] `bar_consecutive_clean()` / `bar_pass_count()` / `bar_last_pass_dirty()`.
- [x] Composer: promotion gate = consecutive clean passes on every encountered bar.
- [x] Composer: `try_reserve` ahead of the special rotation — a bar whose last pass
      was dirty returns at the next planned slot, once per fumbled pass.
- [x] Tests: pass detection across interleaved slots, stray dirtying, promotion
      arc, re-serve scheduling + no-loop guarantee.

## Phase b — tempo ladder

- [x] PlayerModel: per-bar ladder fraction, raised on clean passes, lowered on
      dirty, clamped; persisted.
- [x] Host: cap roll tempo at the entered bar's ladder (piece/review slots only);
      tempo lock and restart-keep-tempo semantics unchanged.
- [x] Tests: ladder movement rules (climb/drop/floor/cap + round-trip).
- [ ] Host cap application test — `apply_tempo_ladder` is thin glue over the
      tested model math, currently verified by smoke only; cover it when the
      host orchestration fixture grows a tempo probe.

## Acceptance

- [x] A bar at a chronic 70% never promotes; three clean passes promote it.
- [x] A fumbled bar reappears in the program ahead of ordinary reviews.
- [x] ctest scoreview suite + smoke green.
