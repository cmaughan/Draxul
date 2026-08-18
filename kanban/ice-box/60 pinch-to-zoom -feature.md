# Trackpad pinch-to-zoom

**Type:** feature
**Priority:** 60
**Raised by:** Gemini

## User need

Use a trackpad pinch gesture to resize the focused pane's content with the same semantics as existing font zoom actions.

## Implementation plan

- [ ] Extend the platform-neutral window event vocabulary with a magnification gesture carrying phase and normalized delta; translate native/SDL events in macOS and Windows window backends.
- [ ] Keep SDL/native constants private to `draxul-window` and ignore gestures already consumed by ImGui/overlays.
- [ ] Route magnification to a focused-host zoom capability; terminal/Nvim/Markdown/ScoreView map it to their existing zoom actions, while unsupported hosts ignore it.
- [ ] Accumulate continuous deltas with hysteresis into bounded zoom steps to avoid jitter and config writes on every tiny update.
- [ ] Coalesce relayout/frame requests and apply the same min/max/reset rules as keyboard zoom.
- [ ] Distinguish pinch from two-finger scrolling and reset gesture state on focus loss/device cancellation.

## Tests and acceptance

- [ ] Unit-test translation, phase cancellation, accumulation thresholds, direction, min/max clamps, focus change, overlay capture, and scroll coexistence.
- [ ] Verify one gesture produces equivalent final host/config state to the corresponding keyboard actions.
- [ ] Manually validate representative macOS and Windows precision touchpads.
- [ ] No gesture reaches a destroyed dispatcher; use the existing guarded callback lifetime.

## Dependencies and parallelism

The callback-lifetime boundary is delivered. A window-event owner can implement
translation while a host-action owner adds capability routing after the event contract is fixed.

<model>GPT-5 Codex</model>
