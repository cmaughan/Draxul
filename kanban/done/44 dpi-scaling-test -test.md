# 44 DPI and Scaling Test

## Why This Exists

`SdlWindow::display_ppi` and the DPI normalisation logic in `app.cpp` translate between logical coordinates, pixel coordinates, and nvim grid units. This involves non-trivial arithmetic and platform differences between macOS (Retina, points vs pixels) and Windows (DPI-aware logical pixels). There are no automated tests for this logic.

Identified by: **Gemini** (testing holes #3).

## Goal

Add unit tests for DPI scaling calculations that mock the window's reported PPI and verify that cell dimensions, grid rows/cols, and renderer pixel sizes are computed correctly for 1x, 1.5x, and 2x scale factors.

## Implementation Plan

- [x] Read `libs/draxul-window/include/draxul/window.h` and `libs/draxul-window/src/sdl_window.cpp` for DPI-related APIs.
- [x] Read `app/app.cpp` for DPI-dependent layout calculations (cell_width_, cell_height_, grid resize).
- [x] Identify which calculations can be extracted into pure functions (no window/renderer dependency).
- [x] Extract DPI/layout math into a testable helper (e.g., `compute_grid_dimensions(ppi, font_metrics, window_px)`) if not already separated. (No extraction needed: `compute_panel_layout` already existed as a free function, and `display_ppi = 96 * scale` is testable inline.)
- [x] Write `tests/dpi_scaling_tests.cpp`:
  - 1.0x scale: verify correct cell pixel size and grid dimensions.
  - 2.0x scale (Retina): verify cell and grid double correctly.
  - 1.5x fractional scale: verify rounding matches expected behaviour.
  - Window resize with constant DPI: verify grid col/row recalculation.
- [x] Wire into `tests/CMakeLists.txt`.
- [x] Run `ctest` and `clang-format`.

## Sub-Agent Split

Single agent. May require a small extraction refactor to make the math testable.
