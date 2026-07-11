# ScoreView Gate — manifesto milestone 2

*Created 2026-07-11. Parent: [scoreview.md](scoreview.md) · North star:
[scoreview-manifesto.md](scoreview-manifesto.md) · Builds on:
[scoreview-conveyor.md](scoreview-conveyor.md).*

The game loop: the conveyor stops advancing on its own and **waits for the
player**. Judgment (right notes light green, wrong notes mark red, the music
moves on), **adaptive tempo** that works at the player's demonstrated pace
within the manifesto's band, and the first **running score**. All player
input arrives through a seam.

> **Acoustic-first commitment.** The product's input is an acoustic piano
> heard through the microphone — that is milestone 3 ("The Ear"), and nothing
> in this milestone may assume anything easier. The keyboard input built here
> is scaffolding to construct and tune the game loop; a MIDI keyboard is at
> most a future convenience option, never the path the product depends on.
> This milestone's deliverable is the loop **and the listener's contract**:
> `IPlayerInput` — timestamped pitch events — is exactly what the microphone
> must produce, and the bot harness built here is exactly how it gets tested.

## The demo (what "done" looks like)

`draxul --host score --source grieg-waltz-op-12-no-2.mxl`, `f` for the
conveyor, then **`g`**:

- The playhead glides to the first notes and **waits**. The status pill shows
  the waiting state.
- Playing the expected note(s) — dev keyboard for now — lights them **green**
  and the conveyor rolls on to the next gate. Chords require all their
  pitches (each matched once; duplicated pitches counted by multiset).
- Playing a wrong note marks the expectation **red**, and once the player has
  *attempted* the gate (played as many notes as it expects) the music **moves
  on** — never stops, never drills; the red stays visible as information.
- **Tie continuations never demand a re-strike**: their gates open on
  arrival (verified identifiable — see foundations).
- Tempo adapts: the controller measures the pace the player actually
  demonstrates between gates and eases toward it, clamped to the
  marking-derived band (25%–120%); long hesitations ease it down.
- A **score and streak** tick in the status pill: points per correct gate,
  weighted by tempo fraction; a miss resets the streak.
- `--command gate-bot` starts a bot that plays the piece through the same
  `IPlayerInput` seam at a configured pace/accuracy — the objective
  verification instrument, and later the regression harness for the ear.

## Verified foundations (measured 2026-07-11, Verovio 6.2.1, Grieg fixture)

- `GetMIDIValuesForElement(id)` → `{pitch, duration, time}`. **All 645
  timemap on-ids return a valid MIDI pitch** (range 33–76), including all 14
  grace notes. Zero misses — expected pitches need **no model bridge**; they
  live in the same id space as the timemap and highlight overlay.
- `GetMEI()` exports the loaded document with **18 `<tie>` elements, 17 of
  which carry an `endid`** (the fixture famously has one unterminated tie —
  Verovio warns "1 ties left open" at import), matching the importer's
  independently-counted 36 tie start/stop marks, in the same id space. The
  set of tie `endid`s = the notes whose gates auto-open.
- Grace-note onsets sit at distinct fractional qstamps (~0.0635 before the
  beat), so they form their own gates ahead of their principal.

## Architecture

```
ILayoutEngine + midi_pitch_for_element(id), tie_end_ids()
        │
        ▼
FlowController (gains TransportMode::Gate)
  armed gate = next unjudged onset
  expected = pitch multiset of its ids  (tie-ends excluded/auto-correct)
  judge(PlayerNoteEvent) → per-id verdicts, gate opens on complete/attempted
  pace EMA → tempo easing within [0.25, 1.2] × marking
  score/streak
        ▲                                   │ verdicts (id → state)
        │ PlayerNoteEvent{midi_pitch, t}    ▼
   IPlayerInput                     ScoreHighlightState (per-op state enum:
   ├── KeyboardPlayerInput (dev)      Passed=amber, Correct=green, Missed=red)
   ├── BotPlayerInput (verification)              │
   └── (M3: MicPlayerInput — the product)         ▼
                                          render_draw_list colors
```

- **One controller, one seam.** Gate mode lives inside FlowController as a
  transport mode; `advance(dt)` still animates the playhead (gliding at the
  current tempo, clamping at the armed gate) and `judge()` consumes input
  events. Milestone 3 changes *which implementation* fills the seam, nothing
  else.
- **Expected notes per gate** are resolved once at build time: for each
  onset, id → MIDI pitch via the engine; ids in the tie-end set are marked
  auto-satisfied (played-anyway counts correct — pianists may re-voice).
- **Judgment semantics** (the manifesto's "never stop"): a gate opens when
  every required pitch is matched (all green) **or** the player has made as
  many attempts as the gate expects (unmatched ids turn red, matched stay
  green). Early input is valid: events always apply to the armed gate, even
  while the playhead is still gliding toward it.
- **Tempo adaptation**: exponential moving average over gate-to-gate
  completion intervals → implied qpm; each gate opening eases the transport
  tempo a fraction toward the clamped implied pace. Stall decay: waiting far
  beyond the current tempo's expectation eases tempo down without requiring
  a gate event. All constants named in one place; tuning is expected and
  cheap because the bot makes pace scenarios reproducible.
- **Scoring (v1)**: per-gate points = base × current tempo fraction of
  marking; full-green gates extend the streak (streak multiplies base up to
  a cap); any red resets the streak. Session-local; persistence is a later
  milestone.
- **Highlight states**: the overlay's per-op flag becomes a state byte
  (None / Passed / Correct / Missed). Clock mode keeps painting Passed
  (amber); gate mode paints verdicts (green/red). One enum, one color map in
  the renderer.

## Non-goals (later milestones)

The microphone listener itself (M3 — see the acoustic-first commitment
above); MIDI hardware input (future option only); the adaptive selection
engine (hands/simplified/motifs); score persistence and HUD polish beyond
the status pill; judging note *durations*/releases (onsets only in M2).

## Phases

### G0 — Expected notes + ties in the engine ✅ (2026-07-11)

- [x] `ILayoutEngine::midi_pitch_for_element(id)` → int (−1 unknown);
  Verovio `GetMIDIValuesForElement` parsed privately (nlohmann)
- [x] `ILayoutEngine::tie_end_ids()` → set of element ids; `GetMEI()` parsed
  privately (tinyxml2) for `<tie endid>` refs
- [x] Tests (live Grieg): all 645 on-ids yield pitches in [21, 108]; tie-end
  set has exactly 17 ids (18 tie elements minus the fixture's unterminated
  one), every one present in the timemap ons

### G1 — Gate transport, judgment, adaptive tempo, score (pure logic) ✅ (2026-07-11)

- [x] Gate table built from onsets: expected pitch multiset per gate,
  required vs auto-satisfied (tie-end) ids
- [x] `TransportMode::Gate`: `advance(dt)` glides and clamps at the armed
  gate; `judge(events)` → per-id verdicts; gate opens on complete or
  attempted; verdict stream consumed like lit diffs today (reset on
  rewind/seek)
- [x] Pace EMA + tempo easing + stall decay, all clamped to the band;
  constants named and documented
- [x] Score/streak state with the v1 rules
- [x] Unit tests: chord multiset matching (incl. duplicate pitches), wrong
  then right, attempted-but-wrong opens with reds, tie-end auto-open,
  early input during glide, rewind resets, **bot simulations**: a perfect
  bot at 50 qpm converges tempo to ~50; an 80%-accuracy bot accumulates
  misses and streak resets; a stalling bot decays tempo

### G2 — Verdict rendering ✅ (2026-07-11)

- [x] `ScoreHighlightState` flags → state enum; renderer color map
  (amber/green/red); clock mode behavior unchanged
- [x] Waiting cue at the playhead (subtle pulse or color shift while gated)
- [x] Unit test: state transitions map to expected colors per op

### G3 — Host integration and inputs ✅ (2026-07-11)

- [x] `IPlayerInput` seam polled in `pump()`; events timestamped on arrival
- [x] `KeyboardPlayerInput` (dev): a qwerty row mapped chromatically around
  the armed gate's register + an oracle key (plays the gate correctly) and a
  wrong-note key — enough to drive and feel the loop by hand
- [x] `BotPlayerInput`: configured pace/accuracy/jitter, deterministic seed;
  `--command gate` / `gate-bot` startup hooks
- [x] `g` toggles gate mode within the conveyor; status pill: waiting/score/
  streak/tempo; smoke stays green
- [x] docs/features.md

### G4 — Objective verification ✅ (2026-07-11)

- [x] Bot screenshot run: green accent pixels grow over time; red pixels
  appear under an error-injecting bot
- [x] Tempo convergence end-to-end: bot at fixed pace → final tempo within
  tolerance, asserted from the INFO log line
- [x] Plan ticks + follow-ups recorded (duration judging, grace-note policy
  review, score persistence)

## Acceptance

The demo above, hand-driven via the keyboard input, feels like the
manifesto's loop (waits, judges, never stops, adapts); all G1 suites green
alongside the full suite and smoke; the bot runs prove green-growth,
red-on-error, and tempo convergence without a human in the loop.

## Risks / notes

- **Grace notes as gates**: required in M2 (they're real notes at their own
  qstamps); if hand-testing shows they make the loop feel pedantic at slow
  tempi, the recorded follow-up is to fold them into their principal's gate
  as optional ornaments.
- **Repeated-pitch chords**: multiset matching is specified and unit-tested
  (a doubled E in both hands needs two E events).
- **Keyboard ergonomics are throwaway**: the dev input exists to exercise
  the loop, not to be good; no effort beyond functional.
- **Input timestamps**: keyboard/bot events use steady_clock on arrival; the
  mic listener will supply its own onset timestamps later — the seam carries
  seconds, not frames, for exactly that reason.
- **Verovio MEI parse** is one string + one tinyxml2 pass at build time;
  negligible next to layout.
