# ScoreView audio output device picker

**Type:** feature
**Priority:** 70
**Raised by:** Claude

## User need

Choose where ScoreView sends metronome, audition, and instrument audio, and recover cleanly if that device disappears.

## Implementation plan

- [ ] Centralize all ScoreView output in `ScoreAudioController` with an injected SDL output-device adapter and one mix stream.
- [ ] Enumerate devices by stable SDL identity plus display name; expose Default and available devices in the Audio inspector.
- [ ] Switch devices through a prepared handoff: pause/control scheduling, open target format, preserve synth/metronome clocks, crossfade or switch at a block boundary, then close the old stream.
- [ ] Detect removal/format failure, fall back to Default, and toast once without stopping scoring/transport.
- [ ] Persist a preferred stable identity/name hint through ScoreView config and retry it on later availability; never fail host initialization solely because it is absent.
- [ ] Route metronome, audition, built-in synth, and soundfont through the same selected stream so components cannot disagree.

## Tests and acceptance

- [ ] Fake-device tests cover enumeration, duplicate names, open failure, hot unplug, fallback, reappearance, rapid switches, multiple hosts, and shutdown mid-switch.
- [ ] Audio cursor/sample scheduling remains monotonic and switch output has no click-sized discontinuity in the deterministic sink.
- [ ] No stream leaks/double-closes and UI/device callbacks do not outlive the controller.
- [ ] Manual output switching works on macOS and Windows.

## Dependencies and parallelism

Depends on bugs 00/14, stress item 16, and preferably ScoreAudioController from item 21. Establish this shared output ownership before layering MIDI reconnect behavior around devices.

<model>GPT-5 Codex</model>
