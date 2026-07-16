# Duplicate a workspace

**Type:** feature
**Priority:** 51
**Raised by:** GPT/Codex

## User need

Clone a useful split layout and its launch descriptors as a new workspace without manually rebuilding every pane.

## Implementation plan

- [ ] Add a pure `clone_workspace_descriptor()` that copies workspace name policy, split topology, pane launch descriptors, working directories, titles, and presentation metadata but not live host/process pointers.
- [ ] Reuse session-state serialization records as the clone currency so duplicate and restore cannot drift.
- [ ] Add `WorkspaceController::duplicate_workspace(id)` as a transaction: validate descriptors, create all hosts, initialize/attach render passes, then publish the workspace only after success.
- [ ] Generate a non-conflicting name such as “Workspace copy” and let inline rename handle customization.
- [ ] Define product-host source behavior explicitly: source file/config descriptors copy; volatile runtime state, terminal buffers, and device ownership do not.
- [ ] Roll back every partially initialized host if any clone pane fails and show the first actionable error.
- [ ] Register a palette/action entry and optional keybinding without hardcoding optional provider availability.

## Tests and acceptance

- [ ] Clone nested split trees containing mixed host kinds and verify independent hosts with equivalent descriptors/layout.
- [ ] Cover missing optional provider, source-file failure, partial host initialization, name collision, and session save/restore.
- [ ] Original workspace state never mutates on clone success or failure.
- [ ] New processes/devices are distinct and each host shuts down exactly once.

## Dependencies and parallelism

Depends on pending 12, 13, 17, and 22. Shares descriptor helpers with layout templates (55), so agree on one serialized layout type before parallel implementation.

<model>GPT-5 Codex</model>
