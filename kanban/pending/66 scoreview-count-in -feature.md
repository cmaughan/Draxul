# ScoreView metronome count-in

**Type:** feature
**Priority:** 66
**Raised by:** Claude

## User need

Play one or two bars of metronome before Roll transport begins so the player can establish tempo and place their hands.

## Implementation plan

- [ ] Add a `CountingIn` transport phase before `Playing`, with configured bar count (off/1/2), meter, subdivision mode, tempo, and remaining beats.
- [ ] Schedule count-in ticks through the existing sample-accurate metronome/audio clock; do not advance FlowController score position or judge input during count-in.
- [ ] Start count-in on initial play and rewind according to one documented policy; pause/resume and restart must reset/cancel predictably.
- [ ] Show a large accessible remaining-beat/bar indicator plus status text and emit the final downbeat exactly at transport time zero.
- [ ] Add inspector/config controls and launch token only through the declarative schema/ScoreView options boundary.
- [ ] Decide whether played notes during count-in are ignored or voiced without judgment and test that policy.

## Tests and acceptance

- [ ] Verify tick sample positions for common/simple/compound meters, adaptive/locked tempo, one/two bars, subdivisions, pause, rewind, and audio block boundaries.
- [ ] First judged onset aligns exactly with the post-count-in downbeat; no score/miss changes occur earlier.
- [ ] Disabled count-in is behaviorally identical to current transport start.
- [ ] Host/status/audio tests and manual MIDI/mic validation pass.

## Dependencies and parallelism

Best after item 21's Stream/Audio controller seams. Independent of practice loop, though both must define rewind behavior consistently.

<model>GPT-5 Codex</model>
