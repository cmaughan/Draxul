# App session rollback fault coverage

**Type:** test
**Priority:** 17
**Raised by:** Claude

## Gap

`App::load_session` and `save_session_as` coordinate workspace state, attach-server identity, metadata, and rollback lambdas. The mid-operation failure paths are not directly pinned.

## Implementation plan

- [ ] Enumerate mutation stages and rollback obligations for load, save-as, rename, attach-server restart, and restore failure.
- [ ] Extend `AppDeps`/session seams only as needed to inject state-store and attach-server failures deterministically.
- [ ] Build fixtures with multiple workspaces/panes, custom names, focus, active session id, and persisted metadata.
- [ ] Fail each stage after the preceding mutation and assert the complete pre-operation state is restored.
- [ ] Assert input host pointers, active workspace id, render tree, and frame requests are valid after rollback.
- [ ] Add success-path assertions alongside failures so the seam cannot test a different flow from production.

## Verification

- [ ] Run the focused tests under ASan where available.
- [ ] Repeat failure cases to detect leaked attach threads, files, or hosts.
- [ ] Run all session/App tests and smoke.

## Acceptance criteria

- [ ] Every rollback stage has named coverage.
- [ ] A failed operation leaves the original live/persisted session usable.
- [ ] No rollback callback captures destroyed or already-moved state.

## Dependencies and parallelism

Safety net before item 22. Coordinate with item 02 if atomic-store injection changes the test seams.

<model>GPT-5 Codex</model>
