# ScoreView worker and device stress suite

**Type:** test
**Priority:** 16
**Raised by:** GPT/Codex, Claude, Gemini

## Gap

ScoreView has several independent background/device edges—engraving, microphone permission/open, RtMidi callbacks, audio output, and instrument switching—but no deterministic suite that drives their interleavings, failure paths, queue bounds, and multi-host ownership.

## Implementation plan

- [ ] Reuse the injected seams from microphone/rebuild fixes and the ScoreHost fixture; never require real devices in the stress suite.
- [ ] Add a fake RtMidi adapter whose constructor, port enumeration, callback registration, cancellation, close, and destructor can throw at controlled points.
- [ ] Add fake input/output device identities and hot-plug events, plus a deterministic audio sink that records scheduled sample positions and discontinuities.
- [ ] Rapidly create/destroy multiple ScoreHosts, switch keyboard/MIDI/microphone, restart/re-engrave, resize, and flood MIDI events while a worker is paused.
- [ ] Define queue/backlog limits for MIDI and audio control events; assert overload drops/coalesces by documented policy instead of growing without bound.
- [ ] Verify instrument switches preserve sample continuity/no click-sized discontinuity and that note release reaches the correct active voice.
- [ ] Add seeded randomized operation sequences and emit the seed/operation trace on failure.

## Tests and acceptance

- [ ] RtMidi failures always fall back cleanly and never escape a destructor as `std::terminate`.
- [ ] Two ScoreHosts do not share or double-close device/stream state accidentally.
- [ ] Every fake callback is cancelled before its target dies; late callbacks are ignored safely.
- [ ] Main-thread operations have a deterministic bounded latency in the blocked-worker fixture.
- [ ] Run repeated normal, ASan, and reopened TSan jobs without races, leaks, hangs, or unbounded queues.

## Dependencies and parallelism

Depends on bugs 00/14 and preferably item 15's fixture. It should complete before item 21 or be used as that refactor's gate. One test owner can work mostly outside `score_host.cpp` once the injected contracts are stable.

<model>GPT-5 Codex</model>
