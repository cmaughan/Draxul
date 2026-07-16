# ScoreView MIDI auto-reconnect

**Type:** feature
**Priority:** 71
**Raised by:** Claude

## User need

If the selected MIDI keyboard is unplugged, report it and reconnect automatically when the same port returns instead of requiring manual combo selection.

## Implementation plan

- [ ] Move MIDI input ownership into `ScoreAudioController`/a dedicated `MidiDeviceSession` using the injected RtMidi adapter from item 16.
- [ ] Represent preference by stable backend identifier where available, with normalized name/index as a documented fallback.
- [ ] Poll or consume backend device-change notifications at a bounded cadence off the main thread, then publish small main-thread events.
- [ ] On disconnect, cancel callbacks before closing, release held notes, fall back to keyboard input, preserve transport/progress, and show one actionable toast/status state.
- [ ] Retry with exponential/backoff-capped cadence and reconnect only to the intended identity; never jump silently to a different same-named device.
- [ ] On reconnect, restore MIDI input and voice routing according to user policy, with an explicit status transition.
- [ ] Stop all retries/callbacks deterministically during host shutdown.

## Tests and acceptance

- [ ] Fake hot-plug tests cover unplug during notes/callback, same device return, same-name different ID, port reorder, repeated failure, rapid flapping, multiple hosts, and shutdown.
- [ ] No stuck notes, callback UAF, unbounded retry loop, UI freeze, or `std::terminate` from RtMidi errors.
- [ ] Scoring/tempo/progress remain intact across fallback and reconnect.
- [ ] Manual validation passes with CoreMIDI and WinMM devices.

## Dependencies and parallelism

Depends on stress item 16, item 21 audio ownership, and preferably output-device item 70's device-session conventions. A MIDI specialist can own it after the controller API is stable.

<model>GPT-5 Codex</model>
