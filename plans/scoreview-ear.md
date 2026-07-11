# ScoreView Ear — manifesto milestone 3

*Created 2026-07-11. Parent: [scoreview.md](scoreview.md) · North star:
[scoreview-manifesto.md](scoreview-manifesto.md) · Builds on:
[scoreview-gate.md](scoreview-gate.md).*

The listener: an **acoustic piano heard through the microphone**, reduced to
the gate's `IPlayerInput` contract — timestamped MIDI pitch events. This is
the product's input (the acoustic-first commitment); MIDI hardware remains a
future convenience at most.

## The one decisive idea

**We never do blind transcription.** The gate knows the expected pitch set,
the key, and roughly the moment. "Did C4+E4+G4 just start?" is a
*verification* problem — near-classical DSP — while "what notes are in this
audio?" is the open research problem. We verify known targets, sweep cheaply
for wrong notes, and keep a neural arbiter as a measured, optional upgrade.
Recent research validates the pairing (score-following + real-time
transcription, arXiv 2505.05078; minimum-latency piano transcription, arXiv
2509.07586).

## The recognition notebook

Facts and design commitments gathered 2026-07-11 — the tuning knowledge base.
Everything here is encoded as named constants in `ListenerTuning` so tests
and future config can adjust without archaeology.

### Piano acoustics

- **Inharmonicity** (physics, not detuning): string stiffness makes partials
  sharp of integer multiples — `f_n ≈ n·f0·√(1 + B·n²)`. B is roughly
  10⁻⁴–10⁻³ in the bass, ~10⁻⁴ mid-keyboard, rising again for the short
  stiff treble strings (10⁻³–10⁻²). Templates must place partials at
  inharmonic positions or bass notes systematically fail; we model B as a
  tunable 3-point curve (bass/mid/treble, log-interpolated).
- **Railsback stretch**: pianos are deliberately stretch-tuned — treble
  sharp, bass flat, up to ±30 cents at the extremes — *on top of* any
  overall drift.
- **Decay**: piano notes are percussive attacks with long exponential decay;
  the sustain pedal lets everything ring. Consequence: score the energy
  **rise at onset**, never absolute energy — that one decision defeats both
  sustained previous notes and pedal wash.
- **Octave/harmonic confusion** is the classic failure: C5's fundamental
  *is* C4's second partial. Mitigations: expected-set scoring, requiring
  energy at odd partials that the lower octave cannot supply, and
  suppressing swept candidates that sit at 12/19/24 semitones above an
  accepted note with no independent partial support.

### Lessons the synthetic suite taught (E1, empirically earned)

- **Ringing notes beat.** Close partials of *different* notes (C4's 9th ≈
  D4's 8th, ~5 Hz apart) swell in and out; every swell is genuine
  narrow-band spectral rise that defeats any flux threshold. A real attack
  is broadband — gate onsets on rise *spread* (count of rising bins,
  `onset_min_rise_bins`), not just flux magnitude.
- **Normalize flux by total spectral energy.** Mic gain and loudness cancel;
  a ringing tone's heavy-tailed jitter stays small relative to its own
  energy while an attack is large by construction.
- **A window-edge peak with the slope rising outward is not our partial** —
  it's the mainlobe skirt of a stronger neighbor outside the window (A4's
  onset reaching into B4's n1 window; parabolic interpolation on a convex
  skirt then *extrapolates into* the kernel). Real partials peak interior.
- **Cents tolerance must be floored in bins.** At A1 one FFT bin spans
  ~580 cents; a pure cents window can't even see low bass partials. Floor
  the half-width at `tolerance_min_bins` (0.6) bins; mid/treble keep the
  cents window or the semitone-neighbor leak reopens.
- **Missing-fundamental phantoms generalize beyond octaves**: an expected
  pitch can score entirely off the dominant note's grid (C4 "heard" under a
  played C5; C5 under F4's coinciding 3rd/6th partials). Acceptance runs an
  anti-phantom gauntlet vs every anchor (accepted notes + sweep's best):
  show **independent partial energy** the anchor can't supply, or — when
  grids fully coincide (octave, twelfth: real chords!) — **excess evidence**
  over the anchor's predicted overtone at the lowest shared partial.
- **Predict anchor overtones from a median, not the fundamental.** A ringing
  neighbor in the before-frames can eat the anchor's measured fundamental
  rise (E4 at 330 Hz suppressed F4's 349 Hz), and a simultaneous chord note
  can inflate single partials. Estimate anchor strength per-partial through
  the amplitude model (`partial_amp_model`), skip windows contested by q's
  own grid, take the median.

### Tuning playbook (E3 — how to tune against a real piano)

Every knob lives in `ListenerTuning`
(`modules/score/draxul-scoreview/include/draxul/scoreview/note_listener.h`);
tests construct variants freely, so tune by adding a fixture case first,
then adjusting defaults. Failure signature → mechanism:

| Symptom | Reach for |
| --- | --- |
| Spurious repeats while a note rings (events every ~refractory gap) | `onset_min_rise_bins` (beats are narrow-band), `onset_threshold_ratio`, `onset_flux_floor` |
| Soft/treble onsets missed | lower `onset_min_rise_bins` (pure tones raise fewer bins), `onset_flux_floor`; check `onset_energy_floor` isn't gating quiet rooms |
| Neighbor semitone/whole-tone accepted at another note's onset | skirt rejection should already kill it — if not, inspect per-partial `hz=` dumps; kernel width = `tolerance_cents`/`tolerance_min_bins` |
| Octave/twelfth phantom accepted | `independent_partial_fraction`, `octave_evidence_ratio`, and especially `partial_amp_model` |
| Real chord note REJECTED at harmonic intervals (C-major!) | excess-evidence too strict: `octave_evidence_ratio` down, or `partial_amp_model` is wrong for that register |
| Bass notes missed | `tolerance_min_bins`, `inharmonicity_bass` (fit B from the recording), `partial_count`/`partial_weights` |
| Detuned piano not recognized / calibration lags | `tolerance_cents`, `calibration_ema_*`, `calibration_clamp_cents` |
| Wrong notes not reported | `wrong_relative`, `suppression_expected_floor` |
| Feels laggy | `confirm_frames_after` (each ≈ 11.6 ms), `hop_size`; measure with the harness's `max_latency_seconds` |

- **`partial_amp_model` (0.6) is the crudest lie in the model.** The synth
  uses exactly `0.6^(n-1)`; real pianos decay differently per register and
  dynamic. If the excess-evidence test misbehaves on real recordings, fit
  the per-register amplitude ratio from the chromatic-scale fixture before
  touching the ratios.
- **Diagnostic recipe** (used for every E1 fix): `DUMP_EVENTS(result)` in a
  failing test prints the event list. For per-onset detail, temporarily
  re-add the env-gated dump in `evaluate_onset` (after the sweep computes
  `best`): under `getenv("DRAXUL_LISTENER_DEBUG")`, print `onset t / best_pitch / best`,
  then for each expected pitch ≥ 0.15×best call `score_pitch` with the
  peaks/rises out-params and print `[n r=<weighted rise> hz=<parabolic peak>]`
  per partial. Run one test (`./build/tests/draxul-tests "listener tracks a scale*"`)
  with `DRAXUL_LISTENER_DEBUG=1`, hand-compare measured vs predicted
  (remember rises are weighted by `partial_weights` — divide before
  comparing to the amp model). Remove the block before committing.
- **Real-recording tests**: `tests/support/wav_reader.h` loads PCM16/float32
  WAVs (mono-mixed); drop fixtures in `tests/fixtures/audio/` and reuse
  `run_listener` with a truth schedule exactly like the synthetic cases.

### Tuning error model (three distinct phenomena, three answers)

1. **Global offset** (whole piano at A≠440): online calibration — every
   confirmed note yields measured-vs-expected cents; an EMA global offset
   shifts all templates. Tolerance windows of ±40–50 cents (semitone spacing
   is 100) mean a quarter-tone-flat piano verifies from the first note and
   is centered within a phrase.
2. **Stretch + per-string quirks**: a **persistent per-piano tuning
   profile** (note → cents, EMA-updated on confirmations). In-memory this
   milestone; persisted per device later. Future UX: "your D4 is 22 cents
   flat."
3. **Inharmonicity**: handled in the template shape itself (above), not the
   tuning offsets.

### Latency budget

Mic buffering ~10 ms + analysis hop ~12 ms + onset-to-pitch confirmation
~35–50 ms ≈ **60–90 ms** perceived — under the ~100 ms feel threshold for
"it heard me". Research shows <30 ms is achievable with causal neural models
if we ever need to chase it (arXiv 2509.07586).

### Library landscape (verified 2026-07-11)

- **No off-the-shelf embeddable "piano note detector" exists** — commercial
  apps (Simply Piano, flowkey, Yousician) all built proprietary engines.
- **SDL3 (already shipped) records audio**: `SDL_OpenAudioDeviceStream` with
  `SDL_AUDIO_DEVICE_DEFAULT_RECORDING`; its stream converts to any
  format/rate we request. Zero new platform dependencies for capture.
- **FFT**: KissFFT (BSD-3, tiny, real-FFT API, pinned releases) via
  FetchContent; Apple vDSP stays a possible mac fast path.
- **Neural arbiter candidates** (only adopted if they measurably beat the
  classical scorer on our fixtures): Spotify **Basic Pitch** (Apache-2.0,
  lightweight, ships ONNX/CoreML/TFLite → ONNX Runtime is MIT);
  piano-specific research: Onsets & Velocities (2023, ~3M params, real-time,
  open source), neural autoregressive transcription (2024), min-latency
  adaptations (2025).
- Avoid: aubio (GPL-3), Essentia (AGPL), madmom (non-commercial clause).

## Architecture

```
SDL3 recording stream ──► mono f32 samples ──► NoteListener (pure DSP, GPU-free)
                                                  ring buffer → STFT frames
                                                  spectral-flux onset detector
                                                  known-target inharmonic
                                                    template scorer (energy RISE)
                                                  88-key wrong-note sweep
                                                  calibration (global + per-note)
                                                        │ PlayerNoteEvent{pitch, t}
                                                        ▼
MicPlayerInput : IPlayerInput ──► FlowController::judge() — unchanged
        ▲ expected pitches per pump (armed gate)
```

- **NoteListener is pure and offline-testable**: samples in, events out, no
  audio device, no clock of its own (time derives from the sample counter).
  Every threshold lives in a `ListenerTuning` struct with documented
  defaults.
- **Expected-set aware, but honest**: the armed gate's pitches focus the
  scorer, and a cheap sweep still reports confident *unexpected* notes —
  wrong notes must reach the judge or the game can't show red.
- **The harness is the method**: WAV/synthetic fixtures → NoteListener → the
  same gate judge, scored for precision/recall/latency in CI — the exact
  discipline the bot established. Candidate listeners (classical now, neural
  later) compete on numbers, on recordings of the actual target piano.
- **v1 threading**: SDL buffers between pumps; the host drains the stream in
  `pump()` (~1–2 ms of analysis per frame). A dedicated analysis thread is a
  recorded optimization, not a v1 requirement.

## Non-goals (this milestone)

Neural inference in-app (evaluation only, via the harness); velocity/dynamics
estimation; duration/release judgment; per-piano profile persistence;
denoising/AGC control beyond sane normalization; non-piano instruments.

## The fixture shopping list (user's piano)

Short phone/USB-mic recordings, dropped in `tests/fixtures/audio/` (WAV,
any common rate, mono or stereo): a slow chromatic scale spanning A1–C6; a
C-major scale hands together; 5–6 isolated chords; the Grieg's first phrase
at practice tempo; the same phrase with 2–3 deliberate wrong notes; 10 s of
room silence. Synthetic fixtures cover CI until these arrive — and after,
both run forever.

## Phases

### E0 — Synthetic truth + offline harness

- [x] `SyntheticPiano` test generator: inharmonic partials (tunable B curve),
  exponential decay, attack transient + 30 ms release ramp (hard cutoffs
  create phantom onsets real pianos never make), global/per-note detuning —
  the controllable lie that lets CI exercise every acoustic fact above
- [x] Minimal WAV reader (PCM16/float32, mono-mix, chunk-skipping) for
  future real fixtures (`tests/support/wav_reader.h` + round-trip test)
- [x] Harness: sample buffer streamed in 10 ms chunks → listener → events vs
  truth schedule, max event latency measured (`run_listener` +
  `DUMP_EVENTS` diagnostics that print only on failure)

### E1 — The classical listener

- [x] `ListenerTuning` (all constants named + documented) and `NoteListener`:
  STFT (KissFFT), energy-normalized spectral-flux onsets (strict local peak,
  adaptive median threshold, rise-spread beat gate, refractory gap),
  onset-gated inharmonic template scoring (bin-floored tolerance windows,
  parabolic peak kernel, skirt rejection, background compensation),
  anti-phantom acceptance gauntlet (independence + excess-evidence vs
  anchors), 88-key sweep with octave suppression for wrong notes,
  global-offset EMA calibration + per-note profile
- [x] Unit suites: single notes, scales, chords (incl. shared-partial
  octaves), detuned piano (recognized immediately, calibration converges),
  inharmonic bass, wrong-note reporting, sustain/pedal overlap, silence and
  noise floors, latency bounds — 11 cases green

### E2 — Live capture in the host

- [x] SDL3 recording stream (request f32 mono 44.1k; SDL converts), drained
  in `pump()`; `MicPlayerInput : IPlayerInput` wiring listener + expected
  set from the armed gate; stale-backlog drop + clock re-anchor after pauses
- [x] macOS `NSMicrophoneUsageDescription` + stable bundle identifier in the
  bundle plist (custom `cmake/MacOSXBundleInfo.plist.in`; TCC keys the
  permission on the id)
- [x] **Async open, permission pre-flight** (the E2 hard lesson, verified by
  stack sample): `SDL_OpenAudioDeviceStream` blocks on the TCC consent
  dialog — synchronously it freezes launch, and even off-thread it holds a
  device lock that deadlocks `SDL_Quit`'s audio teardown. So the opener
  thread first polls AVFoundation (`mic_permission.mm`, async request, no
  SDL locks) and only touches SDL once macOS says yes; shutdown never joins
  (abandoned-exchange handshake owns the stream cleanup either side).
- [x] `--command gate-mic` + `i` toggles mic <-> keyboard mid-session
  (verdicts/score survive); status shows `MIC<level 0-9>` when live,
  `MIC?` while consent is pending; WARN + graceful keyboard fallback when
  the device is denied/unavailable (checked in pump, async-aware).
  Verified live: launch, screenshot (`WAIT MIC? 78qpm (60%)` pill, teal
  playhead), clean exit with the dialog unanswered.
- [x] docs/features.md

### E3 — Tuning against reality

- [ ] Run the harness over the user's real recordings; adjust
  `ListenerTuning` defaults; record before/after metrics in this file
- [ ] Live sessions on the acoustic piano (the felt test): Grieg gates
  end-to-end by ear
- [ ] Record follow-ups: per-piano profile persistence, analysis thread,
  neural arbiter evaluation (Basic Pitch ONNX vs classical on the same
  fixtures), velocity estimation

## Acceptance

Synthetic suites green in CI with explicit precision/recall/latency bounds
(including the detuned and inharmonic pianos); the harness runs real
recordings when supplied and reports metrics; live gate play on the user's
acoustic piano feels responsive (<~100 ms) and judges accurately enough to
be fun — the user's session is the final gate.

## Risks / notes

- **Octave phantoms** are the hardest classical failure; the odd-partial
  requirement + suppression rules are tested from day one, and the neural
  arbiter exists precisely as the escape hatch if classical scoring plateaus.
- **Fast repeated notes** share every partial; onset refractory + rise-based
  scoring should cope, tested explicitly.
- **Room noise / mic AGC** vary wildly; normalization + adaptive floors in
  v1, honest revisit with real recordings.
- **No audio injection into SDL capture** in tests: live path is verified by
  humans; all detection logic is verified offline by construction.
