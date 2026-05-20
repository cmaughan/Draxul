---
# WI 75 — Extract UTF-8 Parsing Detail Functions from `grid.h`

**Type:** refactor  
**Priority:** medium (compile-time improvement; ~100 lines moved from header to TU)  
**Raised by:** [C] Claude  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`libs/draxul-grid/include/draxul/grid.h` contains over 100 lines of `detail::` UTF-8 validation and parsing functions (`utf8_valid_prefix_length`, related helpers). These are implementation details of `CellText::set()` / `CellText::append()`, not part of the grid's public API. Because they live in the header, every translation unit that includes `grid.h` must compile these functions, increasing compilation time for the entire codebase.

---

## Investigation Steps

- [x] Open `libs/draxul-grid/include/draxul/grid.h` and find the `detail::` namespace block
- [x] List all functions and confirm they are only called from `CellText` methods
- [x] Check whether any `CellText` methods are `inline` in the header or if they are already in `grid.cpp`

---

## Implementation Options

**Option A (preferred):** Move `detail::` functions to `libs/draxul-grid/src/grid.cpp` as file-scope static functions. Move any `CellText` member function implementations that call them to `grid.cpp` as well (they should not be inline if they call 100-line helpers).

**Option B:** Create a `libs/draxul-grid/src/utf8_detail.h` (internal, not installed). Include it only from `grid.cpp`. Keeps the detail functions out of the public header but avoids moving them into the same TU if they grow.

---

## Implementation Steps

- [x] Move all `detail::utf8_*` functions to `grid.cpp` (or `utf8_detail.h` if option B)
- [x] Make the relevant `CellText` methods non-inline (move definitions to `grid.cpp`)
- [x] Rebuild; verify no compilation errors
- [x] Run full test suite to verify UTF-8 truncation behaviour is unchanged

---

## Acceptance Criteria

- [x] `grid.h` contains no function bodies in the `detail::` namespace
- [x] `CellText` public API is unchanged (only implementations move)
- [x] All existing grid tests pass
- [x] Build time improvement is measurable (optional to measure; the structural improvement is sufficient)

---

## Notes

Low risk — purely mechanical move. Verify the tests that exercise CellText truncation (`tests/cell_text_test.cpp` or equivalent) all still pass with no changes.
