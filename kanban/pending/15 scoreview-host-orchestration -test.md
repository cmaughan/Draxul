# ScoreView host orchestration coverage

**Type:** test
**Priority:** 15
**Raised by:** GPT/Codex, Claude

## Gap

The unit target links `draxul-scoreview` but not `draxul-scoreview-host`. Pure scoring, notation, slicing, and audio algorithms are tested, while the real host's initialization, window installation, input switching, presentation callbacks, resizing, printing hints, and shutdown are not.

## Implementation plan

- [ ] Add explicit dependency seams to `ScoreHost::Deps` for layout/engraving, time, progress storage, input/device factories, audio output, and frame requests without exposing SDL/Verovio types in the host contract.
- [ ] Create a headless `ScoreHostFixture` with fake NanoVG pass, renderer/frame context, window engraver, audio/input factories, clock, temp progress directory, and MusicXML source.
- [ ] Link the focused test target to `draxul-scoreview-host`; coordinate its final target shape with pending 35 modular tests.
- [ ] Cover initialize/pump/draw/viewport/status/runtime-state/shutdown as one lifecycle and verify callbacks do not outlive the fixture.
- [ ] Exercise paged/flow/roll transitions, async window install and carry state, resize/reflow, input selection, print hint, and frame request behavior.
- [ ] Add failure cases for missing source/resources, worker unavailable, layout failure, audio unavailable, and progress parse failure.

## Tests and acceptance

- [ ] No test requires a physical GPU, microphone, MIDI keyboard, audio output, or user dialog.
- [ ] Delayed fake jobs prove the host keeps rendering the old window until a valid new generation arrives.
- [ ] Shutdown is idempotent from partial initialization and every active mode.
- [ ] Optional-off CMake builds omit the target cleanly; Windows and macOS enabled builds run it through CTest.
- [ ] The fixture becomes the behavioral safety net for item 21 rather than asserting private ScoreHost fields.

## Dependencies and parallelism

Depends on the injection seams from bugs 00 and 14, and feeds item 21. The fixture/test target is a good test-agent task, but coordinate all `ScoreHost::Deps` changes with the one ScoreHost integration owner.

<model>GPT-5 Codex</model>
