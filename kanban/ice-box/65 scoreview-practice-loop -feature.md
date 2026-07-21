# ScoreView practice loop markers

**Type:** feature
**Priority:** 65
**Raised by:** Claude

## User need

Select a bar range and loop it deliberately in Roll mode instead of relying only on the adaptive composer to choose repeats.

## Implementation plan

- [ ] Add a `PracticeRange { first_bar, last_bar }` to `ScoreStreamController`, separate from composer slots and transport position.
- [ ] Expose set-start/set-end/clear actions and inspector controls; avoid reusing `[`/`]`, which already control tempo, unless keybindings are explicitly configurable.
- [ ] Add bar hit testing from engraved measure geometry for click/drag selection, with keyboard/palette fallback for inaccessible layouts.
- [ ] Make manual range policy explicit: loop source bars in order, preserve tempo/verdict/progress attribution, and suspend composer insertion inside the manual loop unless opted in.
- [ ] Use SourceSlicer to request windows covering the range plus bounded look-ahead; wrap seamlessly at the end without installing stale generations.
- [ ] Draw unobtrusive start/end/range markers on the sheet and show loop state in status/inspector.
- [ ] Persist the last range per piece only if desired; always validate against changed score/bar count.

## Tests and acceptance

- [ ] Test one-bar/multi-bar/full-piece ranges, reversed endpoints, range at final bar, restart, mode switch, composer toggle, resize, and async wrap.
- [ ] SourceSlicer corpus tests prove every selected range engraves/judges equivalently to the source.
- [ ] Progress remains attributed to original bar IDs and transport never escapes the active range except on clear.
- [ ] Mouse and keyboard flows are usable on both backends.

## Dependencies and parallelism

Depends on item 19 and preferably ScoreStreamController from item 21. It can proceed independently of audio features after the stream API is stable.

<model>GPT-5 Codex</model>
