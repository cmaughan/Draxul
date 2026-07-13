# ScoreView milestone 4: the runner (roll mode)

The vision correction that supersedes the wait-gate as the game
(2026-07-13, user's words): *"the tempo we are at is maintained; if the
user is missing notes enough (pitch or timing), then we are going to slow
down, and if they are getting them, slowly speed up towards target tempo,
but we are not going to 'stop and wait'. This is more 'guitar hero for
piano'; the user may be fluffing everything, but we keep going (and then
we will be creating music for the user to 'catch up'...). A way to
dynamically create a lesson on an endless runner that enables the user to
slowly improve towards playing the piece."*

## What changes

- **The transport never waits.** `TransportMode::Roll` rolls at the
  current tempo like clock mode; notes judge as the playhead crosses them.
- **Timing is part of correctness.** Each onset has a hit window in beats
  (`kRollEarlyWindowQ` / `kRollLateWindowQ`, ±0.45q) around its crossing:
  the right pitch inside the window is Correct; when the window closes
  with pending required notes, they turn Missed and the music keeps going.
  Stray pitches that match nothing count as wrong notes (tracked
  separately — the practice generator's raw material).
- **Tempo follows demonstrated accuracy, not gate-to-gate pace.** A
  per-note accuracy EMA (`kRollAccuracyAlpha`) drives per-onset easing:
  above `kRollSpeedUpThreshold` the tempo steps up
  (`kRollTempoUpPerOnset`, ~1%/onset) toward the marking band's cap;
  below `kRollSlowDownThreshold` it steps down faster
  (`kRollTempoDownPerOnset`) toward the floor; between, it holds. Still
  clamped to [25%, 120%] of the marking, starting at 60%.
- **Gate (wait) mode survives as a dev/verification instrument** (bots,
  tests, `--command gate`); Roll is the default game at startup and via
  `g` from clock flow.

## Kept deliberately

- The same per-note verdict ledger (gates_/GateNote) and highlight
  pipeline — green/red per note, chords judged per pitch.
- Tie continuations auto-satisfy at their crossing; re-voicing them is
  free, never wrong.
- `IPlayerInput` seam untouched: keyboard, bot, and microphone all feed
  the same judge. `armed_required_pitches()` in Roll = pending required
  pitches whose windows contain the playhead (exactly what the mic
  listener should be primed with).

## Phases

- [x] R0 — FlowController Roll mode + unit tests: window hit/miss/late,
  chord partials, wrong-note counting, tie handling, accuracy-EMA tempo
  easing up/down/hold, clamps, end-of-piece (the last notes get their
  full late window — found by a test tracing the accuracy EMA; 7 cases)
- [x] R1 — Host wiring: Roll is the default game at startup and via `g`;
  status pill shows `>`, acc %, wr count; `--command roll-mic` for the
  runner listening to the piano, `gate*` commands select the wait-mode
  dev instrument. Verified live: default launch rolls, unplayed opening
  notes turn red as they pass, pill read `67qpm (52%) acc 19% miss 10` —
  tempo easing down under total neglect.
- [ ] R2 — Feel pass with the user (window widths, easing rates), then
  the catch-up generation work (milestone 5) builds on the wrong/missed
  note stream this mode produces

## Risks / notes

- Event timestamps are currently mapped to the transport position at
  judge() time (poll cadence ~16 ms — far finer than the ±0.45-beat
  window). If timing scoring ever needs sub-frame precision, carry event
  t_seconds through a time→position map instead.
- Repeats (non-monotonic onsets) still collapse forward as in the
  conveyor milestone.
- The metronome is position-locked, so in Roll it ticks continuously at
  the adaptive tempo — the click IS the pace signal for the player.
