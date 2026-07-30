# App session rollback fault coverage

**Type:** test
**Priority:** 17
**Raised by:** Claude

## Resolution (2026-07-30)

Superseded by server-authoritative topology transactions and checkpoint tests.
The client `App::load_session` / `App::save_session_as` operations no longer
exist; Session create, switch, rename, delete, recovery, and checkpoint failure
behavior is exercised through the server protocol and `ServerKernel` fixtures.

## Gap

`App::load_session` and `save_session_as` coordinate file-backed state,
tab replacement, session identity, and rollback lambdas. Success paths
are covered, but mid-operation storage and restore failures are not directly
pinned.

## Implementation plan

- [ ] Enumerate mutation stages and rollback obligations for load, save-as, and restore failure.
- [ ] Add a narrow injected session-state store only if filesystem fault injection cannot cover a stage deterministically.
- [ ] Build fixtures with multiple tabs/panes, custom names, focus, active session id, and persisted state.
- [ ] Fail each stage after the preceding mutation and assert the complete pre-operation state is restored.
- [ ] Assert input host pointers, active tab id, render tree, and frame requests are valid after rollback.
- [ ] Add success-path assertions alongside failures so the seam cannot test a different flow from production.

## Verification

- [ ] Run the focused tests under ASan where available.
- [ ] Repeat failure cases to detect leaked files or hosts.
- [ ] Run all session/App tests and smoke.

## Acceptance criteria

- [ ] Every file-backed rollback stage has named coverage.
- [ ] A failed operation leaves the original in-memory and persisted session usable.
- [ ] No rollback callback captures destroyed or already-moved state.

## Dependencies and parallelism

Safety net before item 22. Coordinate with item 02 if atomic-store injection changes the test seams.

Item 26 removed attach-server identity, runtime metadata, and attach-thread
rollback obligations. Existing App smoke fixtures now drive real file-backed
save/load paths, so this card can focus strictly on storage and tab
transaction failures.

<model>GPT-5 Codex</model>
