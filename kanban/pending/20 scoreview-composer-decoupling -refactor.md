# Decouple the ScoreView composer for multiple composer implementations

**Type:** refactor
**Priority:** 20
**Raised by:** Claude (Fable 5), composer-separation review 2026-07-16

## Goal

Make the stream composer pluggable so alternative composers can be added without
touching the host — some will rewrite the score, others will modify it, all
generating music to help the learner. `StreamComposer` is already pure and
dependency-injected, but it is not swappable: the host holds the concrete class,
the program/geometry bookkeeping and MusicXML emission live inside the policy
code, and the stream→source provenance mapping is hand-rolled twice in
`score_host.cpp`. Carve those out, put an interface in front, then cut the
GPU-free learning core into its own library so a composer provably cannot reach
Verovio/SDL/transport at link time.

## Design decisions (up front)

- **Host owns the `StreamProgram`; composers extend it** via
  `ensure(StreamProgram&, int slots)`. Composer policy state (cooldowns, drill
  stages) and the program must reset together — the host gets one helper that
  clears both, used by every reset path.
- **Append-only today, rewrite-ready tomorrow.** `StreamProgram` documents the
  committed-history vs open-future invariant: slot geometry at or behind the
  engrave frontier never changes (the stream-q-keyed `verdict_archive_` and the
  window carry depend on it). Future rewriting composers re-plan only the open
  future. This card does NOT implement re-planning — it just keeps the design
  from precluding it.
- **The learning core keeps namespace `draxul::scoreview` and include prefix
  `draxul/scoreview/`** — the library boundary is a link-time contract, not a
  renaming exercise; zero include churn.
- **Out of scope** (deliberately): per-note provenance, composing against
  `notation::ScoreDocument` instead of XML strings, and composer selection via
  config/launch token. This card carves the seams those land into.

## Phase 1 — outcome vocabulary (`note_outcomes.h`)

- [x] Move `NoteVerdict`, `NoteOutcome`, `ChordOutcome` from `flow_controller.h`
      into new `include/draxul/scoreview/note_outcomes.h` as namespace-level
      types.
- [x] Keep compatibility aliases inside `FlowController`
      (`using NoteVerdict = scoreview::NoteVerdict;` etc.) so existing
      `FlowController::NoteVerdict` spellings in the host and tests compile
      unchanged. (Model-layer tests that only touched outcomes switched to the
      namespace spellings; the one test that drives a real FlowController now
      includes it directly.)
- [x] `player_model.h`: include `note_outcomes.h` instead of
      `flow_controller.h`; `apply()` takes the namespace-level types. This cuts
      the model→transport include edge (today it drags `player_input.h`,
      `score_draw_list.h`, `score_timemap.h` into the composer's include graph).

## Phase 2 — `StreamProgram` (program + geometry + provenance)

- [ ] New `stream_program.{h,cpp}`: move `StreamBarPlan` here; add a
      `double source_start_q` field the composer fills at plan time (from
      `SourceSlicer::bar_start_q`) so provenance answers need no slicer.
- [ ] `StreamProgram` owns the plan vector and `slot_start_q_` (moved out of
      `StreamComposer`): `append(plan, quarters)`, `clear()`, `size()`,
      `plan(slot)`, `slot_start_q()/slot_quarters()/slot_at()`, and
      `SourceRef source_at(double stream_q)` returning
      `{drill, source_bar, source_q}`.
- [ ] `StreamComposer::ensure(StreamProgram&, int slots)`; the `try_*` helpers
      append to the passed program; the composer keeps only policy state.
- [ ] Host owns `StreamProgram stream_program_`; swap every
      `composer_.plan/planned/slot_*` read to program reads
      (`build_window_slice`, window-end math, playhead-bar mapping, status
      text, inspector program table, HUD kind badge).
- [ ] Replace BOTH hand-rolled provenance mappings with
      `stream_program_.source_at()`: outcome routing into the player model
      (drill → `PlayerModel::kDrillOnsetSentinel`) and the guidance keyboard's
      `onset_trailing_correct` lookup.
- [ ] Add the paired-reset helper; audit every `composer_.reset()` call site
      (restart_stream, clear-progress, inspector toggle, configure) to clear
      the program alongside.

## Phase 3 — measure writer (`measure_xml.{h,cpp}`)

- [ ] Move the file-local `pitch_xml`, `note_xml`, `parse_chord_key` helpers
      out of `stream_composer.cpp` into a shared writer.
- [ ] Pure fabricators taking primitives: chord drill (broken/block forms) and
      scale bar (tonic/minor/center/divisions/beats).
      `StreamComposer::fabricate_*` become thin resolvers (divisions/beats from
      the slicer, key from the profile) that delegate.
- [ ] This is the "minimal MusicXML writer" named in plans/scoreview-stream.md —
      future tuplet-fidelity work lands here, not in composers.

## Phase 4 — `IComposer` seam

- [ ] New `composer.h`: `IComposer` with `name()`,
      `configure(const SourceSlicer*, const PlayerModel*, const PieceProfile*)`,
      `supports(const SourceSlicer&)`, `reset()`,
      `ensure(StreamProgram&, int slots)`, `finished()` — the whole contract of
      what a composer may observe and produce.
- [ ] `StreamComposer final : public IComposer`; move the single-part gate out
      of the host (`initialize` + inspector checkbox both check
      `slicer_.part_count() == 1` today) into `StreamComposer::supports()` —
      source compatibility is a composer capability, not a host rule.
- [ ] Host holds `std::unique_ptr<IComposer>`; the `composing_` gate becomes
      `composer_enabled_ && stream_windowed_ && composer_->supports(slicer_)`;
      `composer`/`nocomposer` launch tokens and the inspector checkbox behave
      identically.

## Phase 5 — library cut (`draxul-score-learn`)

- [ ] New `modules/score/draxul-score-learn/` static library:
      `note_outcomes.h`, `player_model`, `piece_analysis`, `source_slicer`,
      `stream_program`, `measure_xml`, `composer.h`, `stream_composer`,
      `progress_store`.
- [ ] Links `PRIVATE tinyxml2::tinyxml2 nlohmann_json::nlohmann_json` only
      (verified 2026-07-16: after Phase 1 these files reach no draxul header,
      no logging, no draxul-types) — a leaf library.
- [ ] `draxul-scoreview` links `PUBLIC draxul-score-learn` (its
      `flow_controller.h` includes `note_outcomes.h`); the new subdirectory
      sits inside the existing `DRAXUL_ENABLE_SCOREVIEW` gate; keep the Windows
      CI path valid.
- [ ] Tests keep linking `draxul-scoreview` (transitive); coordinate explicit
      linkage with pending `35 modular-test-targets`.
- [ ] Update `docs/features.md` (module layout) and the S3 notes in
      `plans/scoreview-stream.md`.

## Tests and acceptance

- [ ] One commit per phase; `draxul` + `draxul-tests` build, scoreview `ctest`
      suites, and `py do.py smoke` green at every phase.
- [ ] Phases 1–4 are behavior-preserving: existing composer/stream/roll/gate/
      player-model tests pass with only mechanical API respelling in
      `scoreview_composer_tests.cpp` (program-owned reads).
- [ ] New unit tests: `StreamProgram` geometry + `source_at` (piece, review,
      and drill slots; clamping at both ends; empty program); measure-writer
      golden XML (broken/block drill, dotted-half bass in 3/4, scale bar,
      odd-divisions fallback); `StreamComposer::supports()` (single-part true,
      multi-part false).
- [ ] Provenance equivalence holds: drill outcomes still land on
      `kDrillOnsetSentinel`; a review of bar N still trains bar N's statistics.
- [ ] After Phase 5: `draxul-score-learn` compiles standalone and includes no
      transport, layout, audio, or input header — composer purity is enforced
      by the linker, not by convention.

## Dependencies and parallelism

Land before `21 scoreview-host-decomposition -refactor.md`: its
`ScoreStreamController` should be built around `StreamProgram`/`source_at`
provenance rather than re-extracting them, and this card removes ~40 lines of
hand-rolled mapping from `score_host.cpp` first. The host file is contended
(bugs 00/14, tests 15/16) — phases touch it only mechanically, but land each
phase promptly and avoid parallel feature work in `score_host.cpp` mid-phase.
`19 source-slicer-corpus-equivalence -test.md` is unaffected (the slicer moves
libraries in Phase 5; the test target links it transitively either way).
Ordering: Phase 1 first; Phases 2 and 3 are independent of each other; Phase 4
needs 2; Phase 5 needs all. Future composer implementations (e.g. rewrite-style
generators, and the composer-adjacent `67 scoreview-hand-practice -feature.md`)
plug into the Phase 4 seam.

<model>Claude Fable 5</model>
