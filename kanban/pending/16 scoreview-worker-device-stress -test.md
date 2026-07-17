# ScoreView worker and device stress suite

**Type:** test
**Priority:** 16
**Raised by:** GPT/Codex, Claude, Gemini

## Gap

ScoreView has several independent background/device edges—engraving, microphone permission/open, RtMidi callbacks, audio output, and instrument switching—but no deterministic suite that drives their interleavings, failure paths, queue bounds, and multi-host ownership.

## Implementation plan

- [x] Reuses the shared fixture + injected engraver/engine seams; no real
      device anywhere in the suite (tests/scoreview_worker_stress_tests.cpp).
- [ ] Fake RtMidi adapter with throw injection: needs an IMidiBackend seam in
      MidiPlayerInput — deferred to land with `71 scoreview-midi-auto-
      reconnect`, which introduces that seam anyway. Device-free coverage
      today: feed() floods (two backend threads), kNoDevice fallback paths.
- [ ] Fake device identities/hot-plug + deterministic audio sink: needs an
      SDL-audio ops seam in ScoreAudioController (mirror of IMicrophoneOps) —
      deferred to land with `70 scoreview-audio-output-device`.
- [x] Multi-host create/destroy in both orders, input-switch storm (session
      survives), restyle/restart storm behind a wedged worker (bounded
      main-thread latency, monotonic generations, only the newest installs),
      teardown racing a worker release, MIDI flood while polling slowly.
- [x] MIDI backlog bounded: MidiPlayerInput::kMaxPendingEvents (drop-oldest,
      drop count logged from poll — policy documented in the header) with
      flood tests asserting the cap and freshest-event survival. Audio
      control events remain unbounded-by-design inside the synth ring
      (bounded render blocks); revisit with the card-70 seam.
- [ ] Sample-continuity assertions across instrument switches: needs the
      deterministic audio sink above (the both-voices-render-every-block
      design is in place; the assert awaits the seam).
- [x] Seeded random operation sequence over restyle/restart/poll/input ops;
      seed + full operation trace CAPTUREd for reproduction
      (--rng-seed <seed>).

## Tests and acceptance

- [x] MIDI open failure falls back cleanly (host suite asserts
      requested-vs-engaged); throwing-destructor coverage awaits the
      IMidiBackend fake (card 71).
- [x] Two ScoreHosts verified independent (own engravers/transports,
      destroyed in both orders, ASan-clean under the normal run).
- [x] Late/stale completions are ignored safely (generation filter driven
      directly via the test seam and via the storm).
- [x] Main-thread operations bounded (<100 ms asserted) in the blocked-worker
      fixture.
- [ ] Normal runs green and repeatable (seeded). TSan on this dev machine is
      INCONCLUSIVE: the TSan-built test binary segfaults pre-main in the
      sanitizer runtime (zero output even for single-threaded suites, no
      race reports) — an environment issue, not a finding; run via the
      existing TSan CI wiring instead. ASan job not run here.

## Dependencies and parallelism

Depends on bugs 00/14 and preferably item 15's fixture. It should complete before item 21 or be used as that refactor's gate. One test owner can work mostly outside `score_host.cpp` once the injected contracts are stable.

<model>GPT-5 Codex</model>
