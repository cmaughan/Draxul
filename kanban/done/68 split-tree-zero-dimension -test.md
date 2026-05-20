---
# WI 68 — SplitTree Zero-Dimension Window Tests

**Type:** test  
**Priority:** medium (edge case; could cause downstream assert or UB)  
**Raised by:** [C] Claude  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`SplitTree::recompute()` uses `std::max(0, w - div_w)` when computing sub-viewport widths. A zero-dimension window (width=0, height=0) may still produce unexpected viewport calculations for downstream consumers. This can happen on minimise or during window creation before the first resize event.

---

## Investigation Steps

- [x] Read `app/split_tree.{h,cpp}` — `reset()` calls `recompute()`; `recompute_node()`
  clamps `available = max(0, w - div_w)` for the split direction but passes the
  cross-axis dimension straight through to leaves.
- [x] Traced `recompute(0, 0)` on a two-pane vertical split: leaves get `pixel_size.x == 1`
  (because of the `max(1, ...)` clamp on `first_w`/`second_w`) and `pixel_size.y == 0`.
- [x] **Bug found:** `recompute(-100, -50)` propagates the negative cross-axis dimension
  to every leaf. Fixed by clamping origins and dimensions to `max(0, …)` at the top of
  `SplitTree::recompute(int, int, int, int)` (`app/split_tree.cpp`).

---

## Test Cases

- [x] `reset(0, 0)` on a single-pane tree → leaf descriptor is `{pos=0, size=0}`, no UB.
- [x] `recompute(0, 0)` on a two-pane vertical split → leaves `1×0`, divider at +5px.
- [x] `recompute(0, 0)` on a two-pane horizontal split → mirrored result.
- [x] `recompute(0, 0)` followed by `recompute(1000, 800)` → layout fully recovers.
- [x] `set_divider_ratio(0.5)` then `recompute(0, 0)` → divider rect non-negative on every axis.
- [x] Deeply nested 4-leaf tree at zero size → every leaf descriptor non-negative.
- [x] Negative dimensions clamped at the public API → leaves never see negatives.

---

## Implementation

- [x] Added 7 `[split_tree][zero]` TEST_CASEs to `tests/split_tree_tests.cpp` (82 assertions).
- [x] Production fix: clamp `origin_x/y` and `pixel_w/h` to `max(0, …)` in
  `SplitTree::recompute(int, int, int, int)` so the axis that is *not* the split
  direction can no longer propagate negatives to leaves.
- [x] Build: `cmake --build build --target draxul-tests draxul`
- [x] Run: `ctest --test-dir build -R draxul-tests` (39s, all 542 split_tree assertions pass).

---

## Acceptance Criteria

- [x] All scenarios return non-negative, well-defined viewport structs.
- [x] No assertions fire in debug builds.
- [x] Recover-from-zero test passes — the tree is not permanently corrupted by a
      zero-dimension recompute.

---

## Notes

The "no production code changes expected unless a bug is found" caveat applied —
a real defensive fix was needed for the negative-dimension path. The fix is two
`std::max(0, …)` calls in the public `recompute()` entry point and adds no overhead.
