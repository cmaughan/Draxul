# 46 chord-prefix-visual-indicator -feature

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.claude.md [C]*

## Problem

When the user presses the chord prefix key (e.g., `Ctrl+S`), Draxul enters a "waiting for
second key" state.  There is no visual indicator of this state.  The user cannot tell:

- Whether their key was consumed as a chord prefix or forwarded to nvim.
- Whether the chord system is waiting for a second key.
- Whether the chord timed out or was cancelled.

This is most confusing when the prefix key is also a valid nvim keybinding — the user may
think they pressed the wrong key when in fact the chord system intercepted it.

A minimal implementation is a status chip or status bar entry reading "Prefix: Ctrl+S…" that
appears for the duration of the chord wait, disappearing on resolution or timeout.

## Acceptance Criteria

- [x] When the chord prefix is active, a visible indicator appears in the top-right tab-bar chrome.
- [x] The indicator shows the pressed prefix key and subsequent chord steps (for example `Ctrl+S`, `Ctrl+S 0`, `Ctrl+S 0 2`).
- [x] The indicator disappears when the chord is resolved or times out, using a configurable fade-out.
- [x] The indicator is rendered in the existing top-bar chrome path rather than via ImGui.
- [x] The fade duration is configurable via `config.toml` key `chord_indicator_fade_ms`.
- [x] `docs/features.md` is updated.

## Implementation Notes

The final implementation intentionally diverged from the original ImGui-based proposal:

- The indicator lives in the top-right tab bar, to the left of the CPU/RAM pill, because that
  chrome is always visible and feels more integrated with the workspace/pane controls.
- The `Ctrl+S, 0, 1..9` pane-selection chord shipped at the same time because the visual
  indicator is most useful when multi-step chord state is visible.
- The indicator fade is configurable (`chord_indicator_fade_ms`) rather than the entire feature
  being toggled on/off.
- Render smoke thresholds were loosened slightly to account for the new always-visible top-bar
  chrome.

## Files Touched

- `app/input_dispatcher.h` / `app/input_dispatcher.cpp`
- `app/chrome_host.h` / `app/chrome_host.cpp`
- `app/app.h` / `app/app.cpp`
- `libs/draxul-config/include/draxul/app_config_types.h`
- `libs/draxul-config/src/app_config_io.cpp`
- `docs/features.md`
- `tests/input_dispatcher_routing_tests.cpp`
- `tests/app_config_tests.cpp`

## Verification

- [x] `cmake --build build --target draxul-tests`
- [x] `./build/tests/draxul-tests "[input_dispatcher][chord]"`
- [x] `./build/tests/draxul-tests "app config parse reads all fields"`
- [x] `cmake --build build --target draxul`
- [x] `python3 do.py smoke`

## Interdependencies

- **WI 29** (`input-dispatcher-chord-unit-tests`) should land first to ensure the chord state
  machine is well-understood before adding UI state that depends on it.
- Independent of WI 44, 45 at the code level.
