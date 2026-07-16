# ScoreView session recap

**Type:** feature
**Priority:** 68
**Raised by:** Claude

## User need

Show a concise end-of-practice summary: bars promoted, tempo movement, accuracy/timing, worst chords/pitches, streak, and suggested next focus.

## Implementation plan

- [ ] Add an in-memory `PracticeSessionStats` separate from lifetime `PlayerModel`, recording start/end snapshot, tempo samples, judgments, promoted bars, streak, and trouble changes.
- [ ] Produce a pure `SessionRecap` view model at explicit session end, Roll exit, source change, or user action; application quit should save it but must not block shutdown waiting for UI.
- [ ] Render a keyboard/mouse accessible ScoreView recap state with resume, restart, return to library/paged view, and dismiss actions.
- [ ] Compare session deltas to persisted history without rewriting unknown progress fields or double-counting final flush.
- [ ] Optionally persist only a small bounded recent-recap history after the core view is stable; do not grow progress JSON without a versioned schema.
- [ ] Keep recommendations deterministic and explainable from recorded weak bars/chords rather than introducing opaque scoring.

## Tests and acceptance

- [ ] Test empty/short/full sessions, pause, restart, source switch, clear progress, promoted bars, tempo lock, hand/loop modes, and final flush failure.
- [ ] Recap totals reconcile exactly with events recorded during the session and lifetime progress changes.
- [ ] Dismiss/resume transitions preserve host/device state and application shutdown remains non-blocking.
- [ ] Corrupt historical progress degrades to session-only recap with a warning.

## Dependencies and parallelism

Depends on item 17 and preferably `ScoreSessionController`/view model from item 21. Statistics and presentation can be separate tasks once the recap record is frozen.

<model>GPT-5 Codex</model>
