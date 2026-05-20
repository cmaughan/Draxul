# 02 grid-line-replay-boundary -bug

**Priority:** HIGH
**Type:** Bug (safety/correctness, malformed Neovim redraws)
**Raised by:** GPT
**Model:** claude-sonnet-4-6

---

## Problem

`libs/draxul-nvim/src/ui_events.cpp` (around line 188–249) handles `grid_line` redraw events. It validates the initial `row` and `col_start` coordinates, but then advances `repeat` cells and extra columns for wide glyphs without further bounds checking or clamping. On a malformed or truncated redraw from Neovim, this can result in:

1. Out-of-range write calls against the concrete `Grid` (which currently swallows them silently).
2. Out-of-range write calls against any future `IGridSink` implementation that does not silently clamp — producing undefined behavior or assertion failures.

Neovim itself should not produce malformed redraws in normal operation, but the handler should be defensive since it sits at the RPC boundary.

---

## Fix Plan

- [x] Read `libs/draxul-nvim/src/ui_events.cpp` from the `grid_line` handler entry point. Trace the `repeat` and wide-glyph column advancement loop.
- [x] Identify every place where `col` is incremented beyond `col_start` and check whether the final column value is bounds-checked before the `IGridSink::set_cell()` call.
- [x] Add clamping: before each `set_cell()` call, assert/check `col < grid_width`. If out-of-range, truncate the batch (log a WARN once, not per-cell) and return early from the handler.
- [x] Do the same for `row` on any subsequent `grid_line` entries within the same `redraw` array.
- [x] Write a test (see `07-test`) that replays a malformed `grid_line` batch with `repeat` values that extend past the right edge and verifies no out-of-bounds access occurs.
- [x] Build and run: `cmake --build build --target draxul draxul-tests && py do.py smoke`.

---

## Acceptance

- A `grid_line` batch with `repeat` count that would exceed the grid width is clamped cleanly at the right edge.
- No assertion failures or silent memory writes outside the grid buffer.
- Normal (in-range) `grid_line` events are not affected.

---

## Interdependencies

- `07-test` (UiEventHandler grid_line boundary tests) — should be written alongside this fix.
- No upstream blockers.

---

*claude-sonnet-4-6*
