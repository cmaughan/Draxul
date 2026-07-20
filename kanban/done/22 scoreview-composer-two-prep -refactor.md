# Prepare the composer seam for a second composer

**Type:** refactor
**Priority:** 22
**Raised by:** Claude (Fable 5), duplication review 2026-07-18

## Goal

Close the gaps a second `IComposer` implementation would trip over, found by
a module-wide duplication sweep after the C0–C6 composer work landed. The
seams themselves held up well (C0–C6 and the rewriting composer v1 landed
*through* `IComposer`/`StreamProgram`, not around them); what remains is
copy-paste the next composer would inherit, one silent-failure trap in the
`plan_urgent` contract, and two missing pieces of scaffolding (composer
selection, a composer-agnostic contract suite). Everything here is
behavior-preserving except the new selection seam, whose default leaves
`adaptive-stream` exactly as today.

## Must-haves (gate composer #2)

- [x] **Plan-builder helpers (draxul-score-learn).** The five-line "build a
      plan for bar N and append it" block is hand-assembled ~8× inside
      `stream_composer.cpp` — review-shaped in `try_review` (:376),
      `try_seam` (:433, in a loop), `try_reserve` (:478), `plan_urgent`
      (:501), and the opening-queue drain in `compose_next` (:550);
      drill-shaped in `try_hands`/`try_drill`/`try_scale`. Add two helpers
      beside `StreamProgram` (free functions taking `const SourceSlicer&`):
      `append_source_bar(program, slicer, kind, bar, reason)` and
      `append_fabricated(program, slicer, reference_bar, xml, reason)`
      (plus an insert-variant or an `at_slot` parameter for the splice
      path). ~60 lines removed; composer #2 writes in this vocabulary
      instead of copying the block a ninth time.
- [x] **`SlotCooldowns` helper — mechanize the splice-shift contract.** The
      four `last_*_slot_` maps repeat the identical cooldown gate
      (`find`, compare against `kDrillCooldownSlots`, skip) and
      `plan_urgent` must shift every slot-indexed map by hand via two
      near-identical lambdas (stream_composer.cpp:507). The failure mode is
      silent: a fifth slot-indexed map added without a matching shift line
      quietly corrupts cooldowns after an urgent rewrite. Add a small
      `SlotCooldowns` type (`ready(key, slot)`, `note(key, slot)`,
      `shift_from(slot)`), hold the instances in one list so the splice
      shift is a single loop, and `plan_urgent`'s "keep your bookkeeping
      consistent" comment becomes a mechanism any composer inherits.
- [x] **Composer selection seam.** `ScoreStreamController`'s constructor
      hardcodes `make_unique<StreamComposer>()` — there is no way to choose
      an implementation. Add a boring name-keyed factory (a switch is fine)
      driven by a config key / launch token, defaulting to
      `adaptive-stream`; surface `composer().name()` in the inspector's
      transport section. This is the deliberately deferred follow-up from
      kanban 20 phase 4 — its time has come.
- [x] **Composer-agnostic contract suite.** The 580-line composer test file
      encodes adaptive-stream's *pedagogy*; nothing tests the *seam
      invariants* any `IComposer` must satisfy. Add a suite parameterized
      over an `IComposer` instance (start with `StreamComposer`) asserting:
      program geometry never changes at or behind a given slot across
      `ensure` calls; `source_at` provenance round-trips for every planned
      slot; after `plan_urgent` splices, subsequent `ensure` continues
      coherently and geometry before the splice point is untouched;
      `reset()` + program-clear pairing leaves a re-plannable state;
      `finished()` is sticky. Composer #2 registers into this suite on day
      one and gets the contract for free.

## Cheap sweep (same card, separate slices)

- [x] **Host engrave-tail dedup.** The six-line `EngraveParams` fill appears
      4× (`score_host.cpp` :678 partial, :718, :922, :959) and
      `maybe_urgent_rewrite` duplicates `maybe_advance_stream`'s whole
      slice→params→intent→queue tail. Add
      `EngraveParams ScoreHost::current_engrave_params() const` and a
      `queue_stream_engrave(slice, stream_q, fallback_to_monolith)` helper.
- [x] **`current_source_bar()` helper.** `apply_tempo_ladder` and
      `approx_measure` both hand-roll the same "composing ? `source_at()` :
      divide by quarters-per-bar" dual path — one host helper serves both.
- [x] **One pitch-name table.** The inspector's `note_name`
      (score_host_inspector.cpp:30) duplicates `kPitchNames` in
      piece_analysis. Move `note_name(midi)` into draxul-score-learn beside
      `key_name`; the inspector uses it. `measure_xml`'s
      `kStepNames`/`kStepAlters` stay — step+alter is a different (XML)
      encoding, not a duplicate.
- [x] **Shared engrave test helpers.** `engrave_onsets` is defined verbatim
      in `scoreview_composer_tests.cpp:49` and
      `scoreview_stream_tests.cpp:37`, and five scoreview test files carry
      their own `std::ifstream` fixture loaders (Grieg / Swan Lake). Add
      `tests/support/scoreview_engrave_helpers.h` (fixture reader +
      `engrave_onsets`) and dedupe; every composer-#2 behavior test will
      want exactly these.

## Non-goals (recorded so they are not re-litigated)

- **No composer base class.** `compose_next`'s skeleton (opening drain →
  re-serve → rotating specials → frontier) looks shareable, but a rewriting
  composer may not share it at all. Rule of three: extract only if composer
  #2 actually wants it.
- **Keep the two pedagogy virtuals** (`set_drills_enabled`,
  `set_scales_enabled`) as-is. When a THIRD toggle arrives, fold all of them
  into one `ComposerSettings` struct + single `apply_settings()` instead of
  adding a third virtual — not before.
- **No `bar_mastery`/`bar_encounters` migration onto `BarTally`** unless
  long pieces make composing measurably slow. The C3+ accessors already
  follow compute-once-in-the-model; the older two are O(bars × onsets) per
  compose step, which is fine at current piece sizes.
- **`measure_xml`'s pitch tables stay separate** (see above).

## Tests and acceptance

- [x] Behavior-preserving throughout (helpers are verbatim extractions);
      the selection seam defaults to `adaptive-stream` with unchanged
      behavior. Build + scoreview ctest suites + smoke green per slice.
- [x] Existing composer/stream suites pass with only mechanical helper
      adoption; the contract suite passes for `StreamComposer`, including a
      splice-consistency case that would have caught a missed cooldown
      shift.
- [x] Grep-verifiable end state: one `EngraveParams` fill site in the host,
      one pitch-name table outside `measure_xml`, one `engrave_onsets`
      definition under tests/, no hand-rolled cooldown-gate loops in
      `stream_composer.cpp`.
- [x] A short note in plans/scoreview-composition-model.md or the card
      itself records how composer #2 registers (factory name + contract
      suite hookup).

## Landed (2026-07-20, Claude Fable 5)

All four must-haves and all four cheap-sweep slices implemented
behavior-preserving. Build (app + tests) green; `[scoreview]` ctest is 191
cases / 5762 assertions (incl. the new `[contract]` suite, 6 cases); `do.py
smoke` exits 0. Grep end-state confirmed: one `EngraveParams` fill site
(`ScoreHost::current_engrave_params`, `score_host.cpp`), one pitch-name table
(`kPitchNames` in `piece_analysis.cpp`) outside `measure_xml`, one
`engrave_onsets` definition (`tests/support/scoreview_engrave_helpers.h`), no
hand-rolled cooldown-gate loops in `stream_composer.cpp`.

New seams:

- Plan-builders `append_source_bar` / `append_fabricated` / `insert_source_bar`
  / `insert_fabricated` beside `StreamProgram` (`stream_program.{h,cpp}`).
- `SlotCooldowns<Key>` + `ISlotShift` (`slot_cooldowns.h`); the four cooldowns
  live in `StreamComposer::slot_cooldowns_`, so `plan_urgent` shifts them all in
  one loop.
- `make_composer(name)` factory + `ScoreStreamController::select_composer`;
  `composer().name()` shows in the inspector transport section.
- `tests/scoreview_composer_contract_tests.cpp` (`[contract]`) and
  `tests/support/scoreview_engrave_helpers.h`.

**How composer #2 registers:**

1. Implement `IComposer` in `draxul-score-learn`, writing plans through the
   `append_*` / `insert_*` helpers and holding any slot cooldowns in a
   `SlotCooldowns` registered in a shift list (splice-consistency for free).
2. Add a `case` to `make_composer(name)` in `score_stream_controller.cpp`
   returning the new composer for its name; launch it with
   `--command 'composer=<name>'` (the default stays `adaptive-stream`, and an
   unknown name warns and falls back).
3. Add its factory to `registered_composers()` in
   `scoreview_composer_contract_tests.cpp` — the geometry / provenance / splice
   / reset / finished contract then runs against it automatically; behavior
   tests reuse `scoreview_engrave_helpers.h`.

## Dependencies and parallelism

Builds on kanban 20/21/22 (all landed). Precedes any composer-#2 feature
card — the must-haves are its runway. `stream_composer.cpp` is under active
C-series tuning: land the plan-builder/`SlotCooldowns` slices promptly and
rebase rather than letting them sit. Host slices are mechanical and
independent; the contract suite and test-helper dedupe can run in parallel
with everything else.

<model>Claude Fable 5</model>
