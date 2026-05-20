# 04 grid-scroll-release-ub -bug

**Priority:** MEDIUM
**Type:** Bug
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`libs/draxul-grid/src/grid.cpp:113–128` guards out-of-bounds scroll region parameters with an `assert()`. In Release builds, `assert()` is compiled out, leaving undefined behaviour when the scroll region falls outside grid dimensions. A WARN log is emitted but execution continues into the out-of-bounds path.

---

## Fix Plan

- [x] Read `libs/draxul-grid/src/grid.cpp` around the scroll implementation.
- [x] For each `assert()` guarding scroll bounds:
  - Replace with an explicit bounds check that applies in all build configurations.
  - On violation: clamp the region to valid bounds OR return early with a WARN log — do not proceed with out-of-range values.
  - Keep the Debug-only assert if desired, but always have a Release-safe fallback.
- [x] Check whether similar assert-only guards exist elsewhere in grid.cpp and apply the same fix.
- [x] Build in Release config and run smoke test: `cmake --preset mac-release && cmake --build build --target draxul draxul-tests && py do.py smoke`.
- [x] Run ctest in Release.

---

## Acceptance

- A scroll event with out-of-range region boundaries: WARN logged, operation skipped or clamped, no UB, no crash in Release builds.
- Normal scroll events continue to work correctly.

---

## Interdependencies

- No upstream dependencies.
- Existing test `15 grid-scroll-wide-char-test` should still pass.
