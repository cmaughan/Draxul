# Selected-pane input broadcast

**Type:** feature
**Priority:** 54
**Raised by:** GPT/Codex

## User need

Send keyboard/text input to an explicit group of terminal panes for coordinated interactive work, with strong safeguards against accidental broadcast.

## Implementation plan

- [ ] Add stable pane selection IDs to tab state and commands to add/remove the focused terminal pane from a broadcast group.
- [ ] Keep broadcast routing in `InputDispatcher`/a dedicated router using `HostCapability::TerminalInput`; never infer eligibility from debug names or concrete casts.
- [ ] Require an explicit activation step after selection, display a persistent high-contrast Chrome/status indicator, and provide one-key emergency stop.
- [ ] Broadcast text/key/paste in deterministic pane order while keeping GUI shortcuts, overlays, confirmation dialogs, and mouse input single-targeted.
- [ ] Require confirmation before broadcasting paste, Enter after pasted commands, or destructive control sequences according to a documented policy.
- [ ] Automatically remove closed/restarted panes, disable on tab/session change unless explicitly global, and never persist “active” broadcast across restart.
- [ ] Bound per-pane write queues and report partial delivery rather than silently diverging.

## Tests and acceptance

- [ ] Test selection/activation/deactivation, pane close, focus change, overlays, GUI shortcuts, paste confirmation, partial write failure, and mixed host kinds.
- [ ] Assert only explicitly selected terminal-capable hosts receive events and each receives exactly one ordered copy.
- [ ] A visible indicator is present for every frame while active and broadcast can always be stopped without delivering another host event.
- [ ] No active broadcast state is restored from a saved session.

## Dependencies and parallelism

Foundation cleanup is complete in
`kanban/done/31 foundation-dependency-cleanup -refactor.md`. Depends on finishing the
relevant tab ownership in `kanban/ice-box/22 app-tab-session-controllers -refactor.md`.
Use one input-routing owner.

<model>GPT-5 Codex</model>
