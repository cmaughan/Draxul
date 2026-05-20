# 34 large-paste-stress -test

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.gemini.md [G]*

## Problem

Pasting large text blocks (e.g., 1 MB+) exercises the bracketed paste path, the nvim RPC
write path, and the ring buffer / scrollback eviction path together.  No test verifies that:

1. The main thread remains responsive (does not block for more than one frame budget) while a
   large paste is being sent.
2. The nvim notification queue does not overflow (> 4096 entries) during the write-heavy phase.
3. The scrollback ring correctly evicts old rows rather than leaking memory or corrupting the
   write pointer.

A regression here manifests as a UI freeze on large paste, or as scrambled terminal output
after the paste completes.

## Acceptance Criteria

- [ ] A test sends 500 KB of ASCII text through the bracketed paste path using the fake nvim
      infrastructure (or a replay fixture).
- [ ] The test verifies the notification queue depth never exceeds the drop threshold during
      the send.
- [ ] The test verifies the grid and scrollback ring are consistent after the paste completes.
- [ ] Test runs headlessly without launching a real nvim process.
- [ ] All tests pass under `ctest`.

## Implementation Plan

1. Read `libs/draxul-nvim/src/` for the bracketed paste write path.
2. Read `libs/draxul-nvim/src/rpc.cpp` for the notification queue implementation and its
   drop threshold.
3. Use the replay fixture (`tests/support/replay_fixture.h`) to inject large `grid_line`
   payloads simulating what nvim would emit after a 500 KB paste.
4. Assert queue depth and grid/scrollback consistency after each batch.
5. Run `ctest -R paste_stress`.

## Files Likely Touched

- `tests/large_paste_stress_tests.cpp` (new)
- `tests/CMakeLists.txt`

## Interdependencies

- **WI 41** (`cmake-configure-depends`) should land first.
- Independent of other open WIs.
