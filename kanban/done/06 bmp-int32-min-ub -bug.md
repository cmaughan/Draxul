# WI 06 — bmp-int32-min-ub

**Type:** bug  
**Priority:** MEDIUM (UB from crafted render-test BMP; ASan/UBSan will flag)  
**Platform:** all  
**Source:** review-bugs-consensus.md — BUG-07 (Claude)

---

## Problem

`read_bmp_rgba()` in `libs/draxul-types/src/bmp.cpp` (line 126) calls `std::abs(height)` where `height` is `int32_t` read from an untrusted BMP file. If `height == INT32_MIN` (−2 147 483 648), the mathematical result (2 147 483 648) overflows `int32_t`, making the call undefined behaviour. The existing guard (`height == 0`) does not cover this case. UBSan and ASan will flag this.

---

## Investigation

- [x] Read `libs/draxul-types/src/bmp.cpp` lines 112–130 to confirm the validation logic and the location of the `std::abs` call.
- [x] Confirm the existing guard (`if (... || height == 0)`) and that `INT32_MIN` is not excluded.

---

## Fix Strategy

- [x] Add a guard for `INT32_MIN` before line 126:
  ```cpp
  if (height == INT32_MIN)
      return std::nullopt;
  const int abs_height = std::abs(height);
  ```

---

## Acceptance Criteria

- [x] `read_bmp_rgba()` returns `std::nullopt` for a BMP with `height = 0x80000000`.
- [x] UBSan build does not flag `std::abs(INT32_MIN)`.
- [x] Build and smoke test pass: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
