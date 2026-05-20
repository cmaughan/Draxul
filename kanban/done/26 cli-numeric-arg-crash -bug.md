# 26 cli-numeric-arg-crash -bug

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.gpt.md [P]*

## Problem

`app/main.cpp` uses bare `std::stoi` (no try/catch, no range validation) when parsing the
`--screenshot-delay` and `--screenshot-size` CLI flags.  Passing a non-numeric string or a
value outside `int` range terminates the process with an uncaught `std::invalid_argument` or
`std::out_of_range` exception — producing a crash rather than a clean usage error.

```
./draxul --screenshot-delay abc
terminate called after throwing an instance of 'std::invalid_argument'
```

Neither flag is on a hot path; the fix is a simple try/catch + early-exit with a usage message.

## Acceptance Criteria

- [x] `--screenshot-delay <non-numeric>` prints a usage error to stderr and exits with a
      non-zero code instead of throwing.
- [x] `--screenshot-delay <negative>` prints a usage error (delay cannot be negative).
- [x] `--screenshot-size <non-numeric>` same treatment.
- [x] `--screenshot-size <0>` and `--screenshot-size <negative>` rejected.
- [x] No regression to existing valid flag parsing.

## Implementation Plan

1. Read `app/main.cpp` — locate the two `std::stoi` call sites for these flags.
2. Wrap each in a helper (or inline try/catch) that catches `std::invalid_argument` and
   `std::out_of_range`, prints `"error: --screenshot-delay requires a positive integer"` to
   `stderr`, and calls `std::exit(1)`.
3. Add a lower-bound check (`> 0`) for both values after successful parse.
4. A regression test is filed as WI 33 (`cli-malformed-args -test`); this bug fix is the
   prerequisite.

## Files Likely Touched

- `app/main.cpp` — the only caller site

## Interdependencies

- WI 33 `cli-malformed-args -test` is the regression guard; file that after this fix.
- No other dependencies.
