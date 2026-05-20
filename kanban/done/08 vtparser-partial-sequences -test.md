# 08 vtparser-partial-sequences -test

**Priority:** MEDIUM
**Type:** Test (protocol correctness under partial reads)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`VtParser::feed()` may receive data split at an arbitrary byte boundary (e.g., the OS delivers `"\x1b["` in one read and `"2J"` in the next). There is no test that verifies the parser produces identical output for a sequence split at every possible byte boundary versus delivered as a single chunk. Partial-feed safety is critical: the reader thread delivers chunks at whatever size `read()` returns.

---

## Code Locations

- `tests/terminal_vt_tests.cpp` — VT parser tests
- `libs/draxul-host/src/` or `libs/draxul-host/include/` — `VtParser` class location
- The test should use `VtParser` directly or via `replay_fixture.h`

---

## Implementation Plan

- [x] Read `terminal_vt_tests.cpp` to understand existing parser test patterns and how the parser is instantiated.
- [x] Identify a representative set of CSI sequences to test: `"\x1b[2J"` (clear screen), `"\x1b[1;31m"` (SGR with params), `"\x1b[?1049h"` (DEC private mode), `"\x1b]0;title\x07"` (OSC), and a plain text run.
- [x] For each test sequence, write a parameterised test that:
  1. Feeds the whole sequence in one call — captures emitted callbacks.
  2. Feeds the sequence split at byte index `i` for every `i` from 1 to `len-1` — captures emitted callbacks for each split.
  3. Asserts all split variants produce the same callback sequence as the single-call version.
- [x] Use a simple callback-capture shim (a struct with `std::vector<Event>` that records all emissions).
- [x] Cover the edge case where the split falls in the middle of a multi-byte UTF-8 codepoint within a plain-text run.
- [x] Build and run tests.
- [x] Run `clang-format`.

---

## Acceptance Criteria

- All split variants of the tested sequences produce identical output to single-call delivery.
- The test covers at least 5 distinct sequence types (CSI, OSC, DEC private, SGR with multiple params, plain UTF-8).
- No existing tests are broken.

---

## Interdependencies

- No upstream blockers. Self-contained test addition.
- VtParser is already tested extensively; this adds the split-feed dimension.

---

*claude-sonnet-4-6*
