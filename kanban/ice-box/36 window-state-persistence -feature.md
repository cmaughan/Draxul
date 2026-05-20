# Window State Persistence

**Type:** feature
**Priority:** 36
**Raised by:** Claude

## Summary

Save window position, size, and maximise state to `config.toml` on clean exit, and restore them on next launch. Window size is already partially handled; this work item adds position and maximise state, and makes the persistence robust across platforms.

## Background

Users expect their terminal to reopen where they left it. Draxul currently appears to partially handle window size persistence, but window position and maximise state are not saved. On macOS, window position restoration is expected by the platform HIG. On Windows, restoring the maximise state avoids the window appearing at a default position before being maximised, which causes a visible flash.

## Implementation Plan

### Files to modify
- `app/app.cpp` — add save-on-exit logic: query window position, size, and maximise state from SDL3 before destroying the window; write to config
- `libs/draxul-app-support/` — extend `AppConfig` with `window_x`, `window_y`, `window_width`, `window_height`, `window_maximized` fields
- Config parsing — read/write the window state fields in `config.toml` under a `[window]` table
- `libs/draxul-window/include/draxul/window.h` — add `get_position()`, `get_size()`, `is_maximized()` to the `IWindow` interface if not already present
- `libs/draxul-window/src/sdl_window.cpp` — implement the new interface methods using SDL3 APIs

### Steps
- [ ] Add `[window]` config table fields to `AppConfig`: `x`, `y`, `width`, `height`, `maximized` (all optional, absent means use defaults)
- [ ] In `App::run()` exit path, call `window.get_position()`, `window.get_size()`, `window.is_maximized()` and write the values to the config file
- [ ] On startup, if `[window]` fields are present, pass them to `sdl_window.cpp` during window creation (use `SDL_SetWindowPosition` after creation, or pass to `SDL_CreateWindow` flags)
- [ ] Handle the case where saved position is off-screen (monitor was disconnected): clamp to visible area or reset to default
- [ ] Test: launch, move window, resize, close, relaunch — verify window appears at the saved position and size
- [ ] Test: launch maximised, close, relaunch — verify window restores as maximised

## Depends On
- None (soft dependency on `37 hierarchical-config -feature.md` if per-project window state is desired)

## Blocks
- None

## Notes
Be careful about the off-screen edge case. On Windows, `GetMonitorInfo` can verify the saved position is on a connected monitor. On macOS, `NSScreen.screens` can be used. A simpler approach: if the saved position cannot be placed within any current monitor's work area, fall back to the default (centered on primary monitor).

> Work item produced by: claude-sonnet-4-6
