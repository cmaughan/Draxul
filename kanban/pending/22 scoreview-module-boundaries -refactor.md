# Harden the remaining ScoreView module boundaries

**Type:** refactor
**Priority:** 22
**Raised by:** Claude (Fable 5), composer-separation review 2026-07-16

## Goal

Sweep up the module-boundary findings from the composer-separation review that
belong to neither `20 scoreview-composer-decoupling` (learning-core seams and
library) nor `21 scoreview-host-decomposition` (host controller extraction):
make the verdict archive's keying structural instead of quantized-float, close
the borrowed concrete-pointer seam around player inputs, decide the long-term
composition model (semantic measures vs XML strings), and — opportunistically —
finish the library decomposition of `draxul-scoreview` (audio and input).
Everything here is behavior-preserving except where an item explicitly says
"decision".

Deliberately NOT in this card: host controller extraction (card 21), the
learning-core library and composer seams (card 20), and any split of the
engrave/flow layer — after the other cuts, "Verovio engraving + transport/judge"
is a coherent library and stays together.

## Item 1 — verdict-archive keying (durability)

`ScoreHost::verdict_archive_` maps `(llround(stream_q * 1000), pitch)` →
verdict, written on each outcome and replayed onto the fresh engraving after
every window swap via `FlowController::preset_verdict` (nearest-onset match).
It works because writer and replayer quantize the same axis the same way — but
the invariant is implicit, the replay is nearest-match rather than exact, and a
future rewriting composer that re-plans the open future would silently strand
entries. Fold this into the stream-session state that card 21 moves anyway,
with explicit keying.

- [x] Chose the second option (see below): verdicts are per STREAM position —
      the same source onset legitimately appears at several stream positions
      (piece pass, each review) with different verdicts, so the stream axis is
      the correct key domain; under the committed-history invariant a slot key
      adds no robustness over a quantized stream-q key. Structural re-keying
      dropped as wrong-domain, not merely awkward.
- [x] `VerdictArchive` class (draxul-scoreview): stream-q keying kept, but the
      quantization now lives in ONE private site (`quantize`), with
      round-trip unit tests (tests/scoreview_verdict_archive_tests.cpp) —
      write and replay can no longer disagree about rounding. Host's inline
      map + hand-rolled loops replaced by `record()`/`replay()`.
- [x] Committed-history rule documented on the class (entries recorded as
      judging windows close, never ahead of the playhead; geometry behind the
      engrave frontier never changes) — the invariant future rewriting
      composers rely on (see card 20's design notes).
- [x] Round-trip/window-filter/overwrite/quantum tests pin the replay math to
      the exact pre-refactor semantics (including the window-head tolerance).

## Item 2 — player-input seam (borrowed concrete pointers)

`ScoreHost` owns `std::unique_ptr<IPlayerInput> player_input_` plus three
concrete aliases (`keyboard_input_`, `mic_input_`, `midi_input_`, each
"borrowed from player_input_") and hand-rolls selection/fallback in
`set_gate_input`. The host shouldn't know which concrete input is live; card
21's `ScoreAudioController` owns input lifecycle, so this seam should land as
(or just before) that extraction's first slice.

- [ ] Encapsulate ownership + selection + fallback (mic that fails to open
      falls back to keyboard; MIDI port re-selection) in one input-selector
      component; the host holds that component, not the aliases.
- [ ] Surface per-device needs through narrow capabilities instead of concrete
      types: MIDI port enumeration/selection, mic permission preflight/level,
      keyboard key feed for `handle_gate_key`. Keep `IPlayerInput` itself
      narrow — repo precedent from `15 ihost-interface-width` applies.
- [ ] Behavior preserved: input swaps keep the session (verdicts, score,
      transport); the requested-vs-engaged result reporting is unchanged.

## Item 3 — composition-model decision (two readers, one trajectory)

The module deliberately holds two MusicXML readers: `draxul-notation`'s
semantic importer (`ScoreDocument`, currently used only for status metadata and
notated fifths) and `SourceSlicer`'s verbatim tinyxml2 slicing (the fidelity
trick). The S3 plan points at composers eventually emitting "model measures
with per-note provenance" — i.e. composing against `ScoreDocument` and
serializing through a writer, where tuplet fidelity (Verovio is strict) lives
once. Decide the trajectory BEFORE writing composer #2, because it changes what
`StreamBarPlan` carries.

- [ ] Spike: fabricate one existing drill bar as `notation::ScoreDocument`
      measures and serialize to MusicXML that Verovio engraves identically to
      today's string output; note the gaps (tuplets, voices, backup/forward).
- [ ] Write a one-page decision note (plans/, linked from
      plans/scoreview-stream.md): semantic measures vs XML strings, with the
      per-note-provenance requirement weighed in.
- [ ] If semantic wins: `draxul-notation` becomes a dependency of the
      learning-core library, and card 20's measure writer grows the
      `ScoreDocument` → MusicXML serializer (source bars still slice verbatim).
- [ ] If strings win: record why, and lock the measure writer behind a golden
      MusicXML corpus so tuplet/voice regressions surface in tests, not in
      Verovio.

## Item 4 — audio and input library splits (opportunistic)

After card 20 cuts the learning core out, `draxul-scoreview` still mixes
engrave/flow with audio DSP and device input, each carrying a third-party
dependency. Splitting isolates those deps and shrinks rebuilds; it changes no
behavior. Do this when already touching the CMake wiring — it is the lowest
priority item here.

- [ ] `draxul-score-audio`: `metronome_synth`, `soundfont_synth`,
      `note_listener` (links tinysoundfont, kissfft privately). All pure /
      offline-testable — SDL stays at the host layer, as today.
- [ ] `draxul-score-input`: `player_input.h`, `keyboard_player_input.h`,
      `bot_player_input`, `midi_player_input` (links rtmidi privately;
      mirrors rtmidi's missing include-dir export). `mic_player_input` stays in
      `draxul-scoreview-host` (SDL).
- [ ] Both inside the `DRAXUL_ENABLE_SCOREVIEW` gate; Windows CI path stays
      valid; coordinate test-target linkage with `35 modular-test-targets`.
- [ ] Update `docs/features.md` module layout afterward.

## Tests and acceptance

- [ ] Each item lands independently green: `draxul` + `draxul-tests` build,
      scoreview `ctest` suites, `py do.py smoke`.
- [ ] Item 1: window-swap replay equivalence test passes; the committed-history
      assertion holds through existing stream/roll suites.
- [ ] Item 2: mic-unavailable fallback and MIDI port swap behave identically
      (coordinate coverage with `16 scoreview-worker-device-stress`); no
      concrete input type named in `score_host.cpp` afterward.
- [ ] Item 3: decision note merged and linked; the losing option's rationale is
      recorded so it is not re-litigated per composer.
- [ ] Item 4: no source changes beyond includes/CMake; both platforms build;
      third-party deps (tinysoundfont, kissfft, rtmidi) each appear in exactly
      one library's link list.

## Dependencies and parallelism

Sequenced after `20 scoreview-composer-decoupling` (item 1's structural keys
want `StreamProgram::source_at`; item 4 builds on the learning-core library
cut). Items 1 and 2 should land before or as the first slices of card 21's
`ScoreStreamController` and `ScoreAudioController` respectively — they shrink
what those extractions must move. Item 3 is independent (docs + spike) and
gates future composer implementations (including
`67 scoreview-hand-practice -feature.md` if it grows a composer). Item 4 is
independent of 1–3 and safe to defer. `score_host.cpp` contention rules from
cards 20/21 apply: land slices promptly, no parallel feature work mid-slice.

<model>Claude Fable 5</model>
