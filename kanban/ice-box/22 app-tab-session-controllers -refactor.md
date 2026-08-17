# Extract App tab and session controllers

**Type:** refactor
**Priority:** 22
**Raised by:** GPT/Codex, Claude, Gemini

## Goal

Reduce `App` without recreating the already-completed `InputDispatcher`,
`GuiActionHandler`, or `PaneManager` extraction. The remaining growth is
client-side tab/topology projection and Session navigation; authoritative
Session mutation and persistence belong to the server.

## Delivered checkpoint

`app/tab_controller.*` now owns tab records, active-tab identity, creation,
activation, closure, and per-tab `PaneManager` state. `app/space_controller.*`
owns Space navigation. The remaining work is client topology projection/revision
reconciliation that still lives in `App`.

## Implementation plan

- [x] Preserve active-tab and rollback behavior with the existing controller and App tests.
- [ ] Catalogue `App` fields/methods into bootstrap/frame/overlay, tab, and session responsibilities.
- [x] Extract `TabController` to own tab records, active id, create/close/activate,
      split-tree access, focus transitions, and viewport recomputation.
- [x] Keep window, renderer, and overlay ownership outside `TabController`.
- [ ] Extract a client `SessionProjectionController` for server registry
  refresh, create/switch/rename/delete requests, revision reconciliation, and
  selection. It must not write checkpoints or own shell processes.
- [ ] Replace repeated layout-refresh rituals with one controller operation returning the state App must refresh.
- [ ] Keep `App` responsible for top-level subsystem ownership and the main event/frame loop.
- [x] Land tab extraction independently of the remaining projection work.

## Tests and acceptance

- [ ] Move existing tab/session tests to controller-level fixtures and retain App integration tests.
- [ ] Test multi-tab focus, authoritative revision replacement, rejected
  topology mutations, reconnect/resync, and shutdown ordering.
- [ ] `App` no longer owns tab vectors or session transaction details.
- [ ] No behavior/keybinding/session-format change occurs.
- [ ] Full build, `ctest`, render suite, and smoke pass.

## Dependencies and parallelism

Tab extraction is complete. The remaining projection boundary can be extracted
independently by an App-focused owner with exclusive ownership of `app/app.cpp`.

<model>GPT-5 Codex</model>
