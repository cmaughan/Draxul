---
# WI 64 — CommandPalette Fuzzy Match Scoring Correctness Tests

**Type:** test  
**Priority:** high (untested scoring logic is a palette usability risk)  
**Raised by:** [C] Claude  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`fuzzy_match.cpp` has no dedicated test file. The scoring and match-position logic underpins the command palette's result ordering, but its correctness (exact match > substring > subsequence, shorter target wins ties, match positions correct for highlight rendering) is untested.

---

## Investigation Steps

- [x] Read `app/fuzzy_match.h` and `app/fuzzy_match.cpp` — fzf-style scoring,
  `FuzzyMatchResult { int score; std::vector<size_t> positions; bool matched; }`,
  signature `fuzzy_match(string_view pattern, string_view target)`.
- [x] Read `app/command_palette.cpp` — confirmed the consumer breaks score ties on
  shorter target then lexicographic order (refilter() comparator).

---

## Findings

- **The "exact match scores higher than substring" expectation in this WI was wrong.**
  The current scoring function does **not** penalise unmatched trailing characters in
  the target — `fuzzy_match("split", "split")` and `fuzzy_match("split", "split_vertical")`
  both score 112. The shorter-target preference lives in `command_palette.cpp::refilter()`
  as a sort tiebreaker, not in the score itself. Tests assert this equality so a
  future change to either layer doesn't silently desync.

---

## Implementation

- [x] Created `tests/fuzzy_match_tests.cpp` (auto-discovered by the existing
  `*_tests.cpp` glob in `tests/CMakeLists.txt` — no CMake edit needed).
- [x] 13 Catch2 `TEST_CASE`s under the `[fuzzy_match]` tag, 52 assertions, covering
  all listed scenarios plus the tiebreaker-equality finding.
- [x] Build: `cmake --build build --target draxul-tests`
- [x] Run: `./build/tests/draxul-tests "[fuzzy_match]"` — all pass.

---

## Acceptance Criteria

- [x] All test cases pass (52 assertions, 13 cases).
- [x] At least 5 contiguous-vs-gappy pairs verified (`open`, `close`, `copy`, `new`, `font`).
- [x] Match positions verified strictly increasing and in-bounds.
- [x] No changes to `fuzzy_match.cpp` itself; the WI's incorrect "exact > substring"
      expectation is documented in the Findings section above (no separate WI filed
      since the consumer-side tiebreaker already provides the desired ordering).
