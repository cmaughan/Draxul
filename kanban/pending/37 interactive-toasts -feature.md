# Interactive and actionable toasts

**Type:** feature
**Priority:** 37
**Raised by:** Claude

## User need

Toasts are currently fire-and-forget. Users should be able to dismiss a toast and invoke one safe contextual action such as retry, open log, or show diagnostics.

## Implementation plan

- [ ] Extend the pure toast model with stable id, optional action label/token, dismissibility, and hit regions; keep callbacks out of renderer data.
- [ ] Add pointer hover/click routing in `ToastHost` and consume input only inside visible toast/action regions.
- [ ] Resolve action tokens through an App-owned registry/lifetime token so expired toasts cannot call destroyed objects.
- [ ] Add close affordance, keyboard-focus path, and clear visual hover/focus states.
- [ ] Preserve automatic expiry; define whether interaction pauses expiry and how stacked toasts reflow.
- [ ] Start with internal actions only; do not execute arbitrary strings/commands.
- [ ] Update configuration/docs only if new duration/interaction options are exposed.

## Tests and acceptance

- [ ] Pure layout tests cover close/action hit regions, stacking, DPI, expiry, and reflow.
- [ ] Lifecycle tests cover action once, dismiss, expired token, disabled toasts, and shutdown.
- [ ] Mouse/keyboard interaction does not leak to the focused terminal beneath a toast.
- [ ] Render snapshots and smoke pass on both backend paths.

## Dependencies and parallelism

Prefer item 08 first so labels are Unicode-safe; coordinate with item 23 if Chrome/input routing changes concurrently.

<model>GPT-5 Codex</model>
