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

- [ ] When the chord prefix is active, a visible indicator appears.  Suggested location:
      the bottom status bar, or a small overlay in the bottom-right corner of the focused pane.
- [ ] The indicator shows the pressed prefix key (e.g., "⌃S…" on macOS, "Ctrl+S…" on Windows).
- [ ] The indicator disappears when the chord is resolved (either by a matching second key or
      by cancellation/timeout).
- [ ] The indicator is rendered via ImGui (consistent with the existing diagnostics panel).
- [ ] The feature can be disabled via `config.toml` key `show_chord_indicator = false`.
- [ ] `docs/features.md` is updated.

## Implementation Plan

1. Read `app/input_dispatcher.cpp` to understand how chord prefix state is stored and how
   resolution/cancellation is signalled.
2. Add a method to `InputDispatcher` (or expose a state accessor): `bool chord_pending()` and
   `std::string chord_prefix_display()`.
3. In the ImGui render pass (likely `app/ui_panel.cpp` or the main ImGui overlay), check
   `chord_pending()` and render a small overlay or status text.
4. Add `show_chord_indicator` to `ConfigDocument` (default `true`).
5. Update `docs/features.md`.
6. Manual smoke test: press the chord prefix and verify the indicator appears.

## Files Likely Touched

- `app/input_dispatcher.h` / `app/input_dispatcher.cpp`
- `app/ui_panel.cpp` (or wherever the ImGui overlay lives)
- Config document / AppConfig
- `docs/features.md`

## Interdependencies

- **WI 29** (`input-dispatcher-chord-unit-tests`) should land first to ensure the chord state
  machine is well-understood before adding UI state that depends on it.
- Independent of WI 44, 45 at the code level.
