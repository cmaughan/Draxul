# Extract App workspace and session controllers

**Type:** refactor
**Priority:** 22
**Raised by:** GPT/Codex, Claude, Gemini

## Goal

Reduce `App` without recreating the already-completed `InputDispatcher`, `GuiActionHandler`, or `HostManager` extraction. The remaining growth is workspace ownership and file-backed session orchestration.

## Implementation plan

- [ ] Land item 13 and item 17 so active-workspace and rollback behavior are pinned.
- [ ] Catalogue `App` fields/methods into bootstrap/frame/overlay, workspace, and session responsibilities.
- [ ] Extract `WorkspaceController` to own workspace records, active id, create/close/activate/reorder, split-tree access, focus transitions, and viewport recomputation.
- [ ] Give it narrow callbacks for host construction/frame requests; do not make it own window/renderer/overlays.
- [ ] Extract `SessionController` for file-backed state load/save, list/rename/delete, checkpointing, and load/save rollback; do not reintroduce live-process state.
- [ ] Replace repeated layout-refresh rituals with one controller operation returning the state App must refresh.
- [ ] Keep `App` responsible for top-level subsystem ownership and the main event/frame loop.
- [ ] Land workspace and session extraction as separate commits/PRs.

## Tests and acceptance

- [ ] Move existing workspace/session tests to controller-level fixtures and retain App integration tests.
- [ ] Test multi-workspace focus, last-pane/last-workspace transitions, restore rollback, and shutdown ordering.
- [ ] `App` no longer owns workspace vectors or session transaction details.
- [ ] No behavior/keybinding/session-format change occurs.
- [ ] Full build, `ctest`, render suite, and smoke pass.

## Dependencies and parallelism

Workspace extraction depends on 13/17. The session boundary was simplified by completed item 26 and can now be extracted independently. Large, good for a dedicated App-focused sub-agent with exclusive ownership of `app/app.cpp`.

<model>GPT-5 Codex</model>
