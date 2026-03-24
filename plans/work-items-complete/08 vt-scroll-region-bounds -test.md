# 08 vt-scroll-region-bounds -test

**Type:** test
**Priority:** 8
**Source:** Claude review (review-latest.claude.md)

## Problem

`CSI r` (DECSTBM — set top-and-bottom margins) with invalid parameters (`top >= bot`, or `bot > rows`) should clamp or reject rather than scroll out-of-bounds. The existing `grid_tests.cpp` covers normal scroll behaviour but not the invalid-region case. A misbehaving terminal program or fuzzer input could trigger this path.

## Acceptance Criteria

- [x] Read `tests/grid_tests.cpp` and the VT/CSI `r` handling in `libs/draxul-host/src/` (likely in the terminal emulator or VT parser).
- [x] Add tests using `replay_fixture.h` or direct VT sequence injection for:
  - [x] `CSI 5;3 r` (top > bot) — should be ignored or clamped, not cause out-of-bounds access.
  - [x] `CSI 1;999 r` (bot > rows) — should be clamped to `rows`.
  - [x] `CSI 0;0 r` — should reset to full screen (check VT spec for the correct default).
- [x] Verify under `mac-asan` that no out-of-bounds memory access occurs.
- [x] Run under `ctest`.

## Implementation Notes

- Use `replay_fixture.h` to inject VT sequences without spawning Neovim.
- If clamping is not currently implemented, add it as part of this item.

## Interdependencies

- No blockers. Independent test item.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
