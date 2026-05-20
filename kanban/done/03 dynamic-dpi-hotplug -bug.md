# Dynamic DPI / Monitor-Scale Not Handled After Startup

**Type:** bug
**Priority:** 03
**Raised by:** GPT

## Summary

DPI and display scale are sampled once during initialisation in `app/app.cpp` and `libs/draxul-window/src/sdl_window.cpp`. Moving the Draxul window between monitors with different pixel densities leaves font metrics stale, producing blurry or incorrectly-sized text until the application is restarted.

## Background

Modern systems commonly mix monitors at different DPI scales (e.g., a HiDPI laptop screen alongside a 1080p external monitor). SDL3 provides a `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED` event (or equivalent) to notify applications when the effective scale changes. Draxul does not handle this event. The font metrics, glyph atlas, and cell dimensions computed at startup remain fixed regardless of the actual display scale, causing rendering artefacts when the window is on a monitor with a different scale factor.

## Implementation Plan

### Files to modify
- `libs/draxul-window/src/sdl_window.cpp` — subscribe to the SDL display-scale-changed event and emit it via the window event callback (either extend `WindowResizeEvent` or introduce a new `DisplayScaleEvent` in `libs/draxul-types/include/draxul/events.h`)
- `libs/draxul-types/include/draxul/events.h` — add `DisplayScaleEvent` if a separate event type is chosen
- `app/app.cpp` — handle the new event in the main loop: call font re-initialisation, invalidate the glyph atlas, trigger a full redraw

### Steps
- [x] Identify the correct SDL3 event for display scale change (`SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED` or `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`)
- [x] In `sdl_window.cpp`, subscribe to the event and translate it to either an extended `WindowResizeEvent` carrying the new scale, or a new `DisplayScaleEvent`
- [x] Add `DisplayScaleEvent` to `events.h` if needed; update `IWindow` callback interface accordingly
- [x] In `App`, add a handler for the event that:
  - Re-queries the display scale from the window
  - Re-initialises `TextService` with the new scale
  - Clears and rebuilds the glyph atlas
  - Triggers a full grid redraw
- [ ] Test by moving the window between a HiDPI display and a standard-DPI display

## Depends On
- None

## Blocks
- `13 resize-cascade-integration -test.md` — DPI change follows a similar cascade path

## Notes
The font metrics re-initialisation path should be the same path used on font-size change (`font_increase`/`font_decrease` keybindings), so reuse that code rather than duplicating it. Ensure the glyph atlas is fully invalidated before re-caching glyphs at the new scale.

> Work item produced by: claude-sonnet-4-6
