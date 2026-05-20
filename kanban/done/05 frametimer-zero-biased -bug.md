# 05 frametimer-zero-biased -bug

**Priority:** LOW
**Type:** Bug (misleading metric display to user during startup)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`FrameTimer` (see `frame_timer.h`) maintains a ring buffer of frame durations. Before the ring buffer is fully populated, the rolling average includes uninitialized zero-valued slots, making the reported average frame time appear much lower (and FPS much higher) than actual during the first ~30 frames. The diagnostics panel displays this misleading number to the user on startup.

For a terminal application, frame 1 might take 50ms due to font loading but the average reads near 0ms because 29 of 30 slots are zero.

---

## Code Locations

- `app/frame_timer.h` (or equivalent location) — ring buffer definition and `average_ms()` calculation
- `app/app.cpp` — where `FrameTimer` feeds the diagnostics panel

---

## Implementation Plan

- [x] Read `frame_timer.h` to find the ring buffer size, the insertion index, and the `average_ms()` / `average_fps()` calculation.
- [x] Add a `count_` field (or reuse an existing fill-level counter) that tracks how many valid samples have been inserted since construction or last reset.
- [x] In `average_ms()`, divide by `std::min(count_, ring_buffer_size)` instead of always dividing by the full ring size.
- [x] This ensures the average reflects only real frames until the ring fills.
- [x] No change to the ring buffer insertion logic — only the average calculation.
- [x] Build: `cmake --build build --target draxul draxul-tests && py do.py smoke`
- [x] Run `clang-format` on all modified files.

---

## Acceptance Criteria

- On startup, `average_ms()` returns a value based only on frames that have actually elapsed.
- Once the ring buffer is full, behaviour is identical to before.
- No new memory allocation or complexity.

---

## Interdependencies

- No upstream blockers. Self-contained one-file change.

---

*claude-sonnet-4-6*
