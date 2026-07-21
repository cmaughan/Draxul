# ScoreView manual hand practice

**Type:** feature
**Priority:** 67
**Raised by:** Claude

## User need

Manually isolate left hand, right hand, or both in Roll mode for deliberate practice, using the hand-separation machinery already used by the simplification ladder.

## Implementation plan

- [ ] Define `HandPracticeMode { Both, LeftOnly, RightOnly }` in stream/session state and expose inspector, palette, and configurable shortcut controls.
- [ ] Reuse `SourceSlicer::hands_separate_xml`/semantic staff mapping rather than pitch threshold alone when generating the practice window.
- [ ] Apply the mode consistently to engraved notes, waterfall, guidance keyboard, required pitches, audition, judgment, and progress attribution.
- [ ] Preserve hidden-hand historical progress and tempo when switching modes; record new outcomes against the original bar/hand records.
- [ ] Restart/rebuild through the non-blocking generation path and clearly indicate active hand mode in status.
- [ ] Define behavior for one-staff scores, cross-staff notation, voices that change staff, drills, and composer-generated content.

## Tests and acceptance

- [ ] Corpus fixtures prove left/right extraction retains required attributes, timing, ties, and original provenance.
- [ ] Hidden-hand notes are neither judged nor counted as misses and do not sound in audition unless policy explicitly says otherwise.
- [ ] Rapid mode switching preserves carry state and never installs an old hand-mode generation.
- [ ] Both mode remains identical to current behavior.

## Dependencies and parallelism

Depends on item 19 and preferably item 21's stream boundary. Coordinate with practice loops because both transform source windows; each policy should compose through one request model.

<model>GPT-5 Codex</model>
