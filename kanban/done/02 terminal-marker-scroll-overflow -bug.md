# Clamp scrolling before moving prompt markers

**Summary:** Limit scrolling before moving saved prompt markers so extreme output cannot cause invalid arithmetic.

**Priority:** 69  
**Severity:** CRITICAL  
**Source:** `libs/draxul-terminal-core/src/terminal_core.cpp`

**Evidence and trigger:** B04; line 533 moves markers before clamping. A row-one prompt marker followed by maximum downward scrolling overflows signed arithmetic.

- [x] **Investigate:** Trace upward and downward displacement through parsing, markers, and grid scrolling.
- [x] **Fix:** Clamp once to the valid scrolling region before moving either markers or cells.
- [x] **Acceptance:** Extreme scroll requests produce defined arithmetic and correct marker retention or removal.
- [x] **Validation:** Run core aggregate tests and same-cache smoke.

## Implementation notes (2026-10-05)

- CSI numeric parameters saturate at `INT_MAX`. SU/SD/IL/DL/IND/RI all route
  through `TerminalCore::scroll_rows`, which moved shell marks by the raw count
  (`mark.row -= rows`) before `Grid::scroll` clamped it. A row-one mark with
  `CSI 2147483647 T` (or `L`) computed `1 + INT_MAX`: signed overflow.
- `scroll_rows` now validates the region and clamps the count once to
  `[-region_rows, region_rows]` before moving either marks or cells, so both use
  the same displacement. An invalid region leaves marks untouched (the grid
  already rejects it). ICH/DCH only move cells, and `Grid::scroll` clamps those.
- Regression `terminal core clamps extreme scroll counts before moving shell marks`
  covers maximal SD and IL on a row-one mark, an over-long SU parameter, and marks
  outside a DECSTBM region keeping their rows. The build has no UBSan, so the test
  guards the clamped behavior; the arithmetic itself is now bounded by
  construction.

## Validation (2026-10-05, macOS arm64, Debug make cache `build/`)

- Core aggregate `python3 do.py test debug`: 55/56 CTest entries pass. The one
  failure is `draxul-do-py-tests`, which reads
  `plugins/megacity/product/AGENTS.md`; that submodule is not initialized in this
  worktree, so the failure is environmental. On two earlier aggregate runs under
  heavy machine load (load average 15 to 25 from concurrent builds),
  `app dispatch: shared Neovim split retains actions until its host exists` failed
  its 20 ms `run_smoke_test` pump deadline. That test passed 3/3 shard runs and 8/8
  direct runs both with and without this change, and passed in the final aggregate.
  `server_service_protocol_tests.cpp:567` (`backpressure_elapsed`) failed once
  under load and passed 3/3 when repeated.
- Same-cache smoke `python3 do.py smoke --skip-build`: passed.
- Focused `[terminal-core]` (draxul-test-protocol): 10 cases, 61 assertions pass.
