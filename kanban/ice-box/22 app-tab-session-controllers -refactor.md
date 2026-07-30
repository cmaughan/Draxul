# Extract App tab and session controllers

**Type:** refactor
**Priority:** 22
**Raised by:** GPT/Codex, Claude, Gemini

## Goal

Reduce `App` without recreating the already-completed `InputDispatcher`,
`GuiActionHandler`, or `PaneManager` extraction. The remaining growth is
client-side tab/topology projection and Session navigation; authoritative
Session mutation and persistence belong to the server.

## Implementation plan

- [ ] Land item 13 and item 17 so active-tab and rollback behavior are pinned.
- [ ] Catalogue `App` fields/methods into bootstrap/frame/overlay, tab, and session responsibilities.
- [ ] Extract `TabController` to own tab records, active id, create/close/activate/reorder, split-tree access, focus transitions, and viewport recomputation.
- [ ] Give it narrow callbacks for host construction/frame requests; do not make it own window/renderer/overlays.
- [ ] Extract a client `SessionProjectionController` for server registry
  refresh, create/switch/rename/delete requests, revision reconciliation, and
  selection. It must not write checkpoints or own shell processes.
- [ ] Replace repeated layout-refresh rituals with one controller operation returning the state App must refresh.
- [ ] Keep `App` responsible for top-level subsystem ownership and the main event/frame loop.
- [ ] Land tab and session extraction as separate commits/PRs.

## Tests and acceptance

- [ ] Move existing tab/session tests to controller-level fixtures and retain App integration tests.
- [ ] Test multi-tab focus, authoritative revision replacement, rejected
  topology mutations, reconnect/resync, and shutdown ordering.
- [ ] `App` no longer owns tab vectors or session transaction details.
- [ ] No behavior/keybinding/session-format change occurs.
- [ ] Full build, `ctest`, render suite, and smoke pass.

## Dependencies and parallelism

Tab extraction depends on 13/17. The session boundary was simplified by completed item 26 and can now be extracted independently. Large, good for a dedicated App-focused sub-agent with exclusive ownership of `app/app.cpp`.

<model>GPT-5 Codex</model>
