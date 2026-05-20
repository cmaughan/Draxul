# Grid Scroll Bounds Safety

**Type:** bug
**Priority:** 00
**Raised by:** claude-sonnet-4-6

## Summary

`Grid::scroll()` had only an `assert()` guard — stripped in Release — when the scroll delta exceeds the region. This needed a proper runtime guard for Release builds.

## Background

The `assert(valid && ...)` check for the region bounds was the only protection against out-of-bounds deltas in non-debug builds. Additionally, even with a valid region, the `rows` and `cols` delta values were not clamped to the region dimensions, so a delta larger than the region height/width would produce out-of-bounds index arithmetic.

## Implementation Plan

### Files modified
- `libs/draxul-grid/src/grid.cpp` — added `DRAXUL_LOG_WARN` before invalid-region return, moved `assert` after the warn; added `std::clamp` of `rows` and `cols` to `[-(bot-top), bot-top]` and `[-(right-left), right-left]` with a preceding warn when clamping fires

### Steps
- [x] Read the current `Grid::scroll` implementation
- [x] Keep the `assert()` for Debug builds as an early-catch, add `DRAXUL_LOG_WARN` before the invalid-region early return so Release builds also surface the problem
- [x] Add `std::clamp` of `rows` to `[-(bot-top), bot-top]` and `cols` to `[-(right-left), right-left]` after the region validity check, with a `DRAXUL_LOG_WARN` when the clamp actually fires
- [x] Verify `Grid::scroll()` signature (`void`) — no caller update needed; clamping handles bad input gracefully
- [x] Build: `cmake --build build --target draxul draxul-tests --parallel`
- [x] Run: `./build/tests/draxul-tests` — all grid and scroll tests pass
- [x] Smoke: `python3 do.py smoke` — exit code 0
- [x] Format: `clang-format -i libs/draxul-grid/src/grid.cpp`

## Depends On
- `00 grid-scroll-bounds -bug.md` (parent bug — region validity check was already present)

## Notes
`Grid::scroll()` return type remains `void`. The clamping silently corrects oversize deltas in Release; Debug builds will additionally fire an assert. The warn log ensures visibility in both configurations.

> Work item produced by: claude-sonnet-4-6
