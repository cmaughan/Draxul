# ScoreView composer C4-C6: day-scale spacing, staff hands, on by default

**Type:** feature
**Priority:** 76
**Raised by:** Claude (Opus 4.8), from plans/scoreview-composer.md phases C4-C6

## Delivered

- [x] C4 — PlayerModel tracks civil days (Hinnant days-from-civil parsed from the
      session's ISO stamp) and per-bar day-separated clean streaks; serialized.
- [x] C4 — the composer session opening: bars fumbled on an earlier day return
      first as **overnight re-tests** (sleep consolidates the hardest transitions —
      re-test, don't drill from cold), then **spaced reviews** on the expanding
      1/3/7/14/30-day schedule, most-overdue first, capped at 6 slots. Purity
      kept: the composer reads time through the model's session clock, never a
      wall clock.
- [x] C5 — hand attribution from the engraved staff: the MEI index maps note ids
      to staves (cross-staff notes honour their own @staff), captured at engrave
      time beside the spelling palette (the async engraver means the live engine
      may hold a different document when outcomes drain), carried on NoteOutcome,
      used by the model's hand tallies with the middle-C split as fallback only.
- [x] C6 — composer ON by default; `nocomposer` opts out; `composer` stays as an
      explicit no-op opt-in.
- [x] Tests: civil-day parsing, same-day-never-advances-streak, fumble-resets-
      streak, opening order (re-test before spaced review, today's bars excluded,
      empty history opens on the frontier), staff-over-pitch hand attribution
      (including a hand-crossing case), Grieg MEI staff coverage (every sounding
      note attributed).

## Deferred

- [ ] G7 pre-chunk target model: depends on a count-in window in the
      never-stops transport (kanban/ice-box `66 scoreview-count-in`). Wire the
      audition voice to the count-in when that ships.
