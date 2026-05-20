# 31 sdlwindow-wake-event-ordering -test

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.gpt.md [P]*

## Problem

`SdlWindow` has two under-tested surface areas:

1. **`wake()` + `wait_events()` no-lost-wake**: If a background thread calls `wake()` just
   before the main thread calls `wait_events()`, the wakeup must not be lost.  If `wake()` is
   called while `wait_events()` is already blocking, the block must unblock promptly.  Neither
   race is tested.

2. **Mixed event ordering**: If the event queue receives resize, display-scale-change,
   drop-file, and dialog-result events in rapid succession, the handler should process them
   in arrival order without dropping any.  There is no test for this multi-event scenario.

A lost-wake causes the main thread to block indefinitely when it should have processed a
pending nvim notification.

## Acceptance Criteria

- [ ] A test verifies that calling `wake()` before `wait_events()` causes `wait_events()` to
      return without blocking.
- [ ] A test verifies that calling `wake()` while `wait_events()` is blocking causes it to
      unblock within a reasonable deadline (e.g., 100 ms).
- [ ] A test verifies that enqueuing resize + display-scale + drop-file events in sequence
      results in all three being dispatched in order to registered handlers.
- [ ] Tests run headlessly (no real SDL window creation required, or using the fake window
      infrastructure).

## Implementation Plan

1. Read `libs/draxul-window/src/sdl_window.cpp` and `sdl_window.h` to understand the
   wake mechanism (likely `SDL_PushEvent` with a custom event type).
2. If `SdlWindow` cannot be constructed headlessly, check whether the `IWindow` fake in
   `tests/support/` already covers wake behaviour.  If so, add tests there.
3. If a real `SdlWindow` is needed, explore using `SDL_Init` in test mode without a
   display (SDL3 supports this on some platforms).
4. Write the three tests above.
5. Run `ctest -R sdlwindow`.

## Files Likely Touched

- `tests/sdlwindow_tests.cpp` (new)
- `tests/CMakeLists.txt`

## Interdependencies

- **WI 41** (`cmake-configure-depends`) should land first.
- Independent of other open WIs.
