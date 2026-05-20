# 01 nvim-mouse-viewport-offset — Bug

## Summary

`NvimInput` in `libs/draxul-nvim/src/input.cpp` converts raw pixel mouse coordinates to Neovim grid row/col using only:

```cpp
int col = x / cell_w_;
int row = y / cell_h_;
```

This ignores any viewport origin offset and any padding offset. The rest of the rendering stack (e.g., `libs/draxul-host/src/grid_host_base.cpp` around line 164) consistently adds `viewport_origin + padding` when converting grid positions back to pixel positions. So the mouse coordinate conversion is off by exactly `viewport_origin + padding` pixels — every click is mapped to a cell that is `(viewport_x / cell_w_)` columns and `(viewport_y / cell_h_)` rows earlier than the actual clicked cell.

The unit test in `tests/input_tests.cpp` (~line 61) hardcodes a viewport at (0, 0) with zero padding, so it never catches this discrepancy.

## Steps

- [x] 1. Read `libs/draxul-nvim/src/input.cpp` in full. Locate the mouse button / mouse move event handling that performs the `x / cell_w_`, `y / cell_h_` pixel→grid conversion (approximately line 219).
- [x] 2. Read `libs/draxul-host/src/grid_host_base.cpp` around line 164 to understand how `viewport_origin` and `padding` are stored and named, and how the pixel origin of the grid is computed for the rendering direction (grid pos → pixel).
- [x] 3. Determine how viewport origin and padding are currently passed to (or accessible from) the `NvimInput` class. Check `libs/draxul-nvim/include/draxul/nvim.h` and `NvimInput`'s constructor/members for relevant fields.
- [x] 4. If `NvimInput` does not yet have access to viewport origin and padding:
  - Added `int viewport_x_ = 0; int viewport_y_ = 0;` to `NvimInput`.
  - Added `set_viewport_origin(int x, int y)` setter and `set_grid_size(int cols, int rows)` setter.
- [x] 5. Fix the pixel→grid conversion in `input.cpp` to:
  ```cpp
  int col = (x - viewport_x_) / cell_w_;
  int row = (y - viewport_y_) / cell_h_;
  ```
  Clamp to `[0, grid_cols - 1]` and `[0, grid_rows - 1]` to handle clicks in padding areas.
- [x] 6. Update all call sites in `app/` or `app/input_dispatcher.cpp` that construct or configure `NvimInput` to pass the viewport origin.
  - Updated `nvim_host.cpp`: `on_viewport_changed()` now calls `input_.set_viewport_origin(viewport().pixel_x + renderer().padding(), viewport().pixel_y + renderer().padding())`.
  - Updated `nvim_host.cpp`: `wire_ui_callbacks()` on_grid_resize now calls `input_.set_grid_size(cols, rows)`.
- [x] 7. Read `tests/input_tests.cpp` to find the test(s) that exercise mouse coordinate conversion (~line 61). Identify the fake viewport setup.
- [x] 8. Add a new test case (or update an existing one) with a non-zero viewport origin, e.g., `viewport_x = 8, viewport_y = 12`, and verify that clicking at pixel `(8 + 1 * cell_w_, 12 + 2 * cell_h_)` correctly resolves to col=1, row=2 (not col=1 + 8/cell_w_, row=2 + 12/cell_h_).
- [x] 9. Also add a test that clicks in the padding area (pixel `x < viewport_x`) and verifies the clamped result is col=0.
- [x] 10. Build: `cmake --build build --target draxul draxul-tests`. Confirm no compile errors.
- [x] 11. Run tests: `ctest --test-dir build -R draxul-tests`. Confirm all tests pass including the new ones.
- [x] 12. Run clang-format on all touched files.
- [x] 13. Mark this work item complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- Mouse clicks at a non-zero viewport origin are mapped to the correct Neovim grid cell.
- Clicks in the padding area are clamped to the grid boundary without sending negative row/col values.
- A test with non-zero viewport origin exists and passes.
- All existing tests continue to pass.

*Authored by: claude-sonnet-4-6*
