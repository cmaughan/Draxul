# Detachable pane and workspace windows

**Type:** feature
**Priority:** 43
**Raised by:** GPT/Codex

## User need

Allow a pane or full workspace to move into a new OS window and later rejoin without restarting its host process.

## Architecture phases

- [ ] Land platform callback lifetime item 10 and WorkspaceController item 22.
- [ ] Introduce `AppWindowContext` owning one `IWindow`, renderer/frame context, overlays, input dispatcher, DPI state, and a set of workspaces.
- [ ] Move host/session ownership above individual windows so transferring a workspace does not recreate processes.
- [ ] Phase 1: detach/rejoin an entire workspace; keep pane detachment disabled.
- [ ] Phase 2: move a leaf/subtree between workspace trees using a transactional split-tree transfer.
- [ ] Define focus, active workspace, close-last-window, persistent-app, and session serialization semantics.
- [ ] Create independent Vulkan swapchains/Metal views per window while sharing only resources whose device/lifetime contract permits it.
- [ ] Add commands and visible destinations; support failure rollback if the second window/renderer cannot initialize.

## Tests and acceptance

- [ ] Pure tests cover workspace/subtree transfer, ids, focus, rollback, and session round-trip.
- [ ] Platform integration covers different DPI monitors, closing either window, renderer failure, and application shutdown.
- [ ] Hosts survive detach/rejoin without process restart or lost terminal state.
- [ ] Both Vulkan and Metal paths are implemented before the feature is complete.

## Dependencies and parallelism

Depends on items 10 and 22 and benefits from 23. This is a staged architecture project for a dedicated agent/team, not a single-file SDL change.

<model>GPT-5 Codex</model>
