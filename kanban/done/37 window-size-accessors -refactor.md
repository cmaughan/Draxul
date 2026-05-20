# 37 window-size-accessors -refactor

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.claude.md [C]; additional context review-latest.gpt.md [P]*

## Problem

`IWindow::size_pixels()` (and `size_logical()`) return a pair or struct containing both
width and height.  Call sites in `app/app.cpp` that only need one dimension must destructure
both and suppress the unused binding:

```cpp
auto [pixel_w, pixel_h] = window_->size_pixels();
(void)pixel_h;  // suppress unused-variable warning
```

This pattern appears multiple times.  It is noise that obscures the intent and can confuse
static analysis tools.

The `(void)` suppression also means static analysis cannot flag a genuine unused-variable
bug in the same vicinity.

## Acceptance Criteria

- [x] `IWindow` (and all implementations: `SdlWindow`, `FakeWindow`) gain `width_pixels()`
      and `height_pixels()` accessors (returning `int` or the same unit as `size_pixels()`).
- [x] Similarly for logical units if `size_logical()` exists.
- [x] All `(void)pixel_h;` and `(void)pixel_w;` suppressions in `app/app.cpp` are removed,
      replaced with the single-dimension accessor.
- [x] The existing `size_pixels()` function is kept (do not remove it — it is used where both
      dimensions are needed).
- [x] All existing tests pass.

## Implementation Plan

1. Read `libs/draxul-window/include/draxul/window.h` to understand the current API.
2. Add `width_pixels() const` and `height_pixels() const` as `default` implementations
   calling `size_pixels()`.
3. Add the same to `SdlWindow` and `FakeWindow`.
4. Find all `(void)pixel_h` / `(void)pixel_w` patterns in `app/` and replace with the new
   accessors.
5. Run `cmake --build build --target draxul draxul-tests && py do.py smoke`.

## Files Likely Touched

- `libs/draxul-window/include/draxul/window.h`
- `libs/draxul-window/src/sdl_window.cpp` (if override needed)
- `tests/support/fake_window.h` (or wherever `FakeWindow` lives)
- `app/app.cpp`

## Interdependencies

- Independent of other open WIs.
- **Sub-agent recommendation**: can be paired with WI 36 (both are small, low-blast-radius
  API cleanups).
