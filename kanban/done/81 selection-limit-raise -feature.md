---
# WI 81 — Raise Selection Cell Limit

**Type:** feature  
**Priority:** medium (complement to WI 62 which adds the warning)  
**Raised by:** [C] Claude, [G] Gemini, [P] GPT — unanimous  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`SelectionManager::kSelectionMaxCells = 8192` is a hard compile-time limit on selected cells. On a 200-column terminal, this is only ~40 rows. Users who select code blocks across function boundaries (common in Neovim workflows) hit this limit constantly. WI 62 adds a warning; this item raises or removes the limit.

---

## Status

**Completed** 2026-04-07. Done together with WI 35 (configurable-selection-limit) since exposing it as config requires the same plumbing as raising the constant.

## Investigation Steps

- [x] Read `SelectionManager` — limit was a defensive cap, not memory-driven.
- [x] `kSelectionMaxCells` was only used inside `SelectionManager`; no rendering overlay or clipboard cap relied on the constant.
- [x] Memory cost is trivial at the new default (~128 KB at 65536 cells).
- [x] Clipboard copy path has no separate limit.

---

## Proposed Change

- Raise `kSelectionMaxCells` from 8192 to 65536 (covers ~327 rows at 200 columns — sufficient for any realistic single-selection use case)
- If the limit was defensive (not for memory reasons), make it configurable: `selection_max_cells = 65536` in `[terminal]` config section with a comment about memory implications
- Alternatively, use a `std::vector` that grows dynamically up to a large configurable cap (removes the hard constant entirely)

---

## Implementation Steps

- [x] Replaced `static constexpr int kSelectionMaxCells = 8192` with an instance member `int max_cells_` (default `kDefaultSelectionMaxCells = 65536`, range 256–1048576).
- [x] Added `set_max_cells()` accessor; `LocalTerminalHost::initialize` applies `launch_options().selection_max_cells` after the base initializes.
- [x] Updated `tests/selection_truncation_tests.cpp` to use `kDefaultSelectionMaxCells`.
- [x] Existing truncation warning path still fires at the new threshold.
- [x] Made it configurable via `[terminal] selection_max_cells` (range 256–1048576), see WI 35.

## Acceptance Criteria

- [x] 40+ rows × 200 cols selection succeeds without truncation.
- [x] `kSelectionMaxCells` is now runtime-configurable.
- [x] All selection tests pass.
- [x] Truncation warning still fires at the configured limit.

---

## Notes

**Interdependency with WI 62:** WI 62 adds the warning callback when the limit is hit. This item raises the limit. They can be done independently but landing WI 62 first means users get feedback in the interim. Doing them in the same pass is also fine.
