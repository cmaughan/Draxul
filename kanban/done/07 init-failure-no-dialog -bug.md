# No Error Dialog on Initialisation Failure

**Type:** bug
**Priority:** 07
**Raised by:** Claude

## Summary

`App::initialize()` in `app/app.cpp` returns error codes for renderer initialisation failure and font load failure, but only the nvim-not-found case (work item #59) shows a user-facing dialog. Other init failures are silent: the application exits without any indication of what went wrong, leaving users unable to diagnose the problem.

## Background

Work item #59 added an error dialog specifically for the case where `nvim` is not found on PATH. However, renderer init failure (Vulkan/Metal device unavailable), font load failure (font file missing or corrupt), and window creation failure all return errors from `App::initialize()` without surfacing a dialog. On platforms where these failures are plausible (e.g., missing Vulkan drivers, misconfigured font path in config.toml), users see only a silent crash with no actionable message.

## Implementation Plan

### Files to modify
- `app/app.cpp` — extend `App::initialize()` error handling to show a platform dialog for each failure case (renderer, font, window)
- `app/app.cpp` — ensure the dialog is shown before any partial cleanup, and that partial cleanup does not itself crash when components are partially initialised

### Steps
- [x] Review the `App::initialize()` error path and list every return-error case that does not currently show a dialog
- [x] For each un-dialogued failure, add a call to the same dialog mechanism used by the nvim-not-found case
- [x] Craft a clear, actionable message for each failure:
  - Renderer init failure: include the platform name (Vulkan/Metal) and suggest driver update
  - Font load failure: include the font path from config and suggest checking config.toml
  - Window creation failure: generic SDL error with the SDL error string
- [x] Ensure partial initialisation state is handled safely: dialog must be callable even if window/renderer is not yet available (use a fallback OS-native dialog call if needed)
- [ ] Test each path by temporarily forcing the failure condition

## Depends On
- None

## Blocks
- `22 startup-rollback-clipboard -test.md` — the test covers this path

## Notes
On Windows, use `MessageBoxW` as a fallback dialog if the SDL window is not yet created. On macOS, use `NSAlert` or `CFUserNotificationDisplayAlert`. This ensures the error is always visible regardless of how far initialisation progressed.

> Work item produced by: claude-sonnet-4-6
