# WI 94 — grid-line-repeat-unbounded

**Type:** bug  
**Priority:** 2 (CPU DoS / main-thread stall)  
**Source:** review-bugs-consensus.md §M1 [Claude]  
**Produced by:** claude-sonnet-4-6

---

## Problem

In `libs/draxul-nvim/src/ui_events.cpp:264–279`, `repeat` is read from the msgpack `grid_line` cell array and validated only with a lower-bound check (`< 1`). An unbounded upper value (e.g., `repeat = 65535`) causes 65535 iterations of `grid_->set_cell()` per cell entry, stalling the main thread for tens of milliseconds and causing visible frame drops. A malformed or adversarial server can trivially trigger this.

---

## Investigation

- [ ] Read `libs/draxul-nvim/src/ui_events.cpp:240–285` — confirm the `repeat` validation and the loop that calls `set_cell`.
- [ ] Check what the neovim protocol specifies as the maximum valid `repeat` value (it should be at most `grid_cols - col`).

---

## Fix Strategy

- [ ] Clamp `repeat` to the remaining columns in the current row:
  ```cpp
  repeat = std::clamp(repeat, 1, std::max(1, grid_cols - col));
  ```
- [ ] Log a warning (DEBUG level) when clamping occurs.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] `repeat` is always clamped to `[1, grid_cols - col]`.
- [ ] A `repeat = 65535` value in a malformed packet does not stall the main thread measurably.
- [ ] Smoke test passes.
