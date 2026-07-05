# Enforce the active-workspace invariant

**Type:** bug
**Priority:** 13
**Raised by:** Claude

## Problem

When `active_workspace_` is not found, `App::active_host_manager()` returns a mutable process-static dummy. Callers can silently mutate shared fallback state and hide the real lifecycle error.

## Implementation plan

- [ ] Add `find_active_workspace()` pointer/optional helpers and one checked `require_active_workspace()` for invariant-required paths.
- [ ] Remove both mutable and const static dummy managers.
- [ ] Audit initialization, last-pane close, workspace removal, session restore rollback, and shutdown to state when no active workspace is legal.
- [ ] Make optional call sites handle absence explicitly; make invariant-required call sites fail loudly with context.
- [ ] Set the next active id before destroying the current workspace and centralize that transition.

## Tests

- [ ] Cover closing active/non-active/last workspaces, failed restore, stale active id, and shutdown with no workspace.
- [ ] Prove no host/pane mutations disappear into fallback storage.

## Acceptance criteria

- [ ] No process-static `HostManager` fallback exists.
- [ ] Every no-workspace state is explicit and tested.
- [ ] App/session/workspace tests, `ctest`, and smoke pass.

## Dependencies and parallelism

Land before item 22 so the extracted workspace controller starts with a real invariant.

<model>GPT-5 Codex</model>
