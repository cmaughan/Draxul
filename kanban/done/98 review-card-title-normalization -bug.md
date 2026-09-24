# Recover consensus cards with labeled titles

**Type:** bug
**Raised by:** 2026-09-24 bug review publication failure

## Problem

The review runner rejects otherwise complete proposed Kanban cards when the
provider writes a `**Title:**` line instead of a Markdown level-one heading.
The 2026-09-24 bug consensus finished, but publication stopped before writing
its summary and 28 cards.

## Work

- [x] Normalize a labeled title into a level-one heading in each proposed card.
- [x] Keep the raw provider response archived and retain strict validation for cards without a usable title.
- [x] Add a focused regression test.
- [x] Recover the saved consensus without another provider call and verify summary and cards.
- [x] Run the repository aggregate test and same-cache smoke gates.

## Notes

- Review run: `plans/reviews/runs/20260924T074533Z-review-bugs-7985d9ba`.
- Original saved response: `plans/reviews/runs/20260924T081348Z-review-bugs-summary-0e25f00e/response.md`.
- Fable's saved response was only a completion notice; the consensus explicitly states the findings rely on Astra's substantive report.
- `review.py` now converts a complete `**Title:**` line into the `#` heading
  required by Kanban cards while leaving the archived provider response intact.
  Cards without either title form still fail validation.
- Recovered summary: `plans/reviews/runs/20260924T083605Z-review-bugs-summary-recovered-6daaa928/summary.md`.
  The recovery command created all 28 proposed cards from the saved Codex
  session, and the original review manifest now links to that recovered run.
- The focused review runner suite passed 40 tests. `python3 do.py test debug`
  passed all 25 aggregate entries; `python3 do.py smoke debug --skip-build`
  passed immediately afterward with Metal initialization on Apple M5.
