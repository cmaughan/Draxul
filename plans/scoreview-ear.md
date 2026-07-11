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

- [ ] `SyntheticPiano` test generator: inharmonic partials (tunable B curve),
  exponential decay, attack transient, global/per-note detuning — the
  controllable lie that lets CI exercise every acoustic fact above
- [ ] Minimal WAV reader (PCM16/float, mono-mix) for future real fixtures
- [ ] Harness: sample buffer → listener → events vs truth schedule →
  precision / recall / median latency report

### E1 — The classical listener

- [ ] `ListenerTuning` (all constants named + documented) and `NoteListener`:
  STFT (KissFFT), spectral-flux onset detection with adaptive threshold and
  refractory gap, onset-gated inharmonic template scoring at expected
  pitches, 88-key sweep with octave suppression for wrong notes,
  global-offset EMA calibration + per-note profile
- [ ] Unit suites: single notes, scales, chords (incl. shared-partial
  octaves), detuned piano (recognized immediately, calibration converges),
  inharmonic bass, wrong-note reporting, sustain/pedal overlap, silence and
  noise floors, latency bounds

### E2 — Live capture in the host

- [ ] SDL3 recording stream (request f32 mono 44.1k; SDL converts), drained
  in `pump()`; `MicPlayerInput : IPlayerInput` wiring listener + expected
  set from the armed gate
- [ ] macOS `NSMicrophoneUsageDescription` in the bundle plist (TCC prompt)
- [ ] `--command gate-mic` + a key to switch input source in gate mode;
  status shows MIC + input level; graceful fallback to keyboard when the
  device is unavailable
- [ ] docs/features.md

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
