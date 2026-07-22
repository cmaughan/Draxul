# Tab and pane locks

**Type:** feature
**Priority:** 57
**Raised by:** GPT/Codex

## User need

Protect important long-running panes or complete tabs from accidental close, restart, replacement, or destructive layout actions.

## Implementation plan

- [ ] Add `locked` metadata to pane/tab controller records and versioned session state; locking a tab protects all current panes plus tab deletion.
- [ ] Centralize destructive-operation authorization in `TabController`/PaneManager so palette, keybinding, window close, layout template, move, and session actions cannot bypass it.
- [ ] Define explicit force/unlock flows with confirmation and clear reason text; application quit may summarize locked busy panes rather than silently ignoring them.
- [ ] Show lock icons/status affordances and palette actions for lock/unlock without reducing pane-title hit targets.
- [ ] Decide persistence policy explicitly (recommended: persist locks, never persist a pending confirmation) and handle older sessions as unlocked.
- [ ] Keep locks local UI policy; they do not claim the child process or file is protected from external termination.

## Tests and acceptance

- [ ] Exercise every destructive entry point against pane lock, tab lock, both, force, unlock, restore, and corrupted metadata.
- [ ] Non-destructive focus/move-within-policy/render/input operations still work.
- [ ] No action can close/restart/replace a locked target without the documented confirmation/force path.
- [ ] Chrome/state/session tests and smoke pass on both platforms.

## Dependencies and parallelism

Depends on the `TabController` refactor (`ice-box/22 app-tab-session-controllers -refactor.md`), currently deferred to the ice box, and coordinates with busy-process close guard (44). Implement one shared close policy rather than stacking independent confirmation dialogs.

<model>GPT-5 Codex</model>
