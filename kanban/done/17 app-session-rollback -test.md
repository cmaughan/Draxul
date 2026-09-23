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

- [x] Superseded: the removed App load/save-as transaction has no remaining
      mutation stages or rollback obligations. The server now accepts topology
      commands atomically and checkpoints an already accepted revision.
- [x] No App session-store injection seam is needed. `ServerKernel` fixtures
      deterministically inject checkpoint write failures and topology fixtures
      construct malformed/insertion-failure cases directly.
- [x] Replacement fixtures exercise multiple Spaces, tabs, panes, names, split
      trees, terminal identities, revisions, and durable checkpoint state.
- [x] `destination insertion failure preserves both trees` and adjacent malformed
      command cases assert that a rejected topology mutation leaves the complete
      snapshot unchanged.
- [x] Client projection/focus validity is covered by the topology projection and
      two-client detach/move/reconnect cases; server-owned hosts remain attached to
      stable pane/process identities.
- [x] Success paths run beside the failure sections in the same topology and
      checkpoint fixtures, including a successful live pane move before an
      injected asynchronous checkpoint failure.

## Verification

- [x] Keep failure-path checks deterministic and runnable in the normal suite.
- [x] Replacement failure cases use isolated temporary runtimes and verify cleanup,
      preserved last-good files, resumed saving, and usable restored hosts in the
      normal suite; the deleted App rollback callback cannot leak files or hosts.
- [x] The replacement suites are included in the normal core aggregate. The final
      macOS aggregate/smoke and Windows 48/48 aggregate/same-cache smoke are
      recorded by cards 12 and 13.

## Acceptance criteria

- [x] Every current failure boundary has named coverage: topology rejection is
      atomic; checkpoint failure preserves the prior durable file; corrupt input
      is archived; partial restore remains checkpointable.
- [x] An asynchronous checkpoint failure preserves the accepted live topology and
      the previous durable topology independently, and later recovery remains
      usable.
- [x] The App rollback callbacks named by this card were removed. Current topology
      commands own values through the server transaction and do not retain
      callbacks over destroyed or moved client state.

## Shared-server audit (2026-09-23)

The original requested seam would test an operation that no longer exists. Current
coverage is split at the actual ownership boundary:

- `tests/server_topology_terminal_tests.cpp` checks rejected commands against an
  unchanged complete snapshot, including destination insertion failure preserving
  both source and destination trees. It also checks multi-client projection and
  focus after detach, move, and reconnect.
- `tests/server_session_checkpoint_tests.cpp` checks last-good-file preservation,
  asynchronous write failure after an accepted move, isolated failure between
  Sessions, corrupt-checkpoint archival, and writable partial restore.
- `tests/client_recovery_tests.cpp` checks client projection refresh after a server
  revision rollback.

There is deliberately no injected `App::load_session`/`save_session_as` store or
rollback-lambda test: those entry points and callback lifetime hazards were deleted
when Session ownership moved to the server. The local `App` file-state helpers used
by non-shared tests do not participate in the production shared-server transaction.

## Dependencies and parallelism

Safety net before item 22. Coordinate with item 02 if atomic-store injection changes the test seams.

Item 26 removed attach-server identity, runtime metadata, and attach-thread
rollback obligations. Existing App smoke fixtures now drive real file-backed
save/load paths, so this card can focus strictly on storage and tab
transaction failures.

<model>GPT-5 Codex</model>
