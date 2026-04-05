# WI 86 — signed-overflow-pixel-size-calculations

**Type:** bug  
**Priority:** 0 (undefined behaviour / OOB read at large image sizes)  
**Source:** review-bugs-consensus.md §C5 [Claude]  
**Produced by:** claude-sonnet-4-6

---

## Problem

Two locations compute pixel buffer sizes or row offsets as `int * int * int`, then cast the result to `size_t`. Signed integer overflow is undefined behaviour in C++; at image dimensions above ~46 341 pixels the result is wrong.

1. **`libs/draxul-types/include/draxul/types.h:92`** — `CapturedFrame::valid()`:
   ```cpp
   rgba.size() == static_cast<size_t>(width * height * 4)
   ```
   The inner `width * height * 4` overflows before the cast.

2. **`libs/draxul-types/src/bmp.cpp:137–138`** — BMP row offset:
   ```cpp
   const auto src_row = pixel_offset + static_cast<size_t>(src_y * width * 4);
   const auto dst_row = static_cast<size_t>(y * width * 4);
   ```
   Same signed overflow before cast; produces an incorrect (wrapping) row offset.

---

## Investigation

- [ ] Read `libs/draxul-types/include/draxul/types.h:85–94` — confirm the exact expression and that no wider type is used elsewhere in `CapturedFrame`.
- [ ] Read `libs/draxul-types/src/bmp.cpp:125–148` — confirm the row offset calculation and any downstream uses of `src_row` / `dst_row`.
- [ ] Check whether `width` and `height` in `CapturedFrame` are bounded anywhere at a safe max before these calculations are reached.

---

## Fix Strategy

- [ ] `types.h:92` — cast operands to `size_t` before multiplication:
  ```cpp
  return width > 0 && height > 0
      && rgba.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
  ```
- [ ] `bmp.cpp:137–138` — same pattern:
  ```cpp
  const auto src_row = pixel_offset + static_cast<size_t>(src_y) * static_cast<size_t>(width) * 4u;
  const auto dst_row = static_cast<size_t>(y) * static_cast<size_t>(width) * 4u;
  ```
- [ ] Grep for any other `width * height * 4` or `row * cols * N` patterns in draxul-types and draxul-renderer that use signed arithmetic before a `size_t` cast; fix the same way.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] No `int * int` arithmetic appears before a `size_t` cast in `CapturedFrame::valid()` or `bmp.cpp` row offset calculations.
- [ ] `CapturedFrame::valid()` returns `true` for a correctly sized RGBA buffer regardless of image dimensions.
- [ ] Smoke test passes.
