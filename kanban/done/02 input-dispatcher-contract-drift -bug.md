# 02 input-dispatcher-contract-drift -bug

**Type:** bug
**Priority:** 2
**Source:** GPT review (review-latest.gpt.md)

## Problem

The contract comment in `app/input_dispatcher.h` (around line 23) states that text-editing events are forwarded to both `UiPanel` and the host. The implementation in `app/input_dispatcher.cpp` (around line 224) only forwards text-editing events to the host — `UiPanel` does not receive them.

This is a documentation/contract mismatch now, but it is precisely how IME-related regressions are introduced later. When IME composition work (icebox 29) is implemented, a developer will read the contract comment and assume the `UiPanel` path is already exercised. It is not.

## Acceptance Criteria

- [x] Read `app/input_dispatcher.h` and `app/input_dispatcher.cpp` in full.
- [x] Determine which is correct: should `UiPanel` receive text-editing events, or should the comment be updated?
  - [x] If `UiPanel` **should** receive them: add the forwarding call.
  - [x] If `UiPanel` **should not** receive them: update the comment to match reality.
- [x] Grep for any other comment/implementation divergences in `InputDispatcher` (mouse routing, text suppression, pixel-scale) while the file is open.
- [x] Update comments to accurately reflect the actual dispatch rules.
- [x] Build and run `ctest`.

## Implementation Notes

- This is primarily a documentation correctness fix. If the forwarding call is added, it is very small.
- Do not restructure the dispatcher logic — that is `03 input-dispatcher-e2e-routing -test` territory.
- A sub-agent is appropriate for this item: read the two files, determine the correct fix, apply it.

## Interdependencies

- **Should be done before** `03 input-dispatcher-e2e-routing -test` so the test is written against the correct contract.
- **Prerequisite for** icebox 29 (IME composition visibility) being done correctly.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
