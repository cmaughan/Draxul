# Degrade instead of failing when projected topology cannot be applied

**Type:** bug
**Priority:** P1 / sequence 24
**Raised by:** Claude (server/client runtime review, 2026-07-30)
**Evidence:** [plans/reviews/2026-07-30-server-client-runtime-review.md](../../plans/reviews/2026-07-30-server-client-runtime-review.md#topology-apply-failure-handling)

## Goal

A shared Session containing something this client cannot render must not stop the client from
starting, and a failed apply must not leave the window permanently out of sync.

## Observed behaviour

**An unknown host kind hard-fails the client.** `project_remote_tab` returns false with
"Unsupported projected host kind '<kind>'" (`app/app.cpp:3617-3629`). At startup that
propagates out of `initialize_remote_topology` → `App::initialize` fails → no window ever
appears (`app/main.cpp:780`). At runtime `apply_remote_topology_tabs` returns false
mid-application, so earlier Spaces and tabs are already mutated, stale-tab cleanup and the
trailing `input_dispatcher_.set_host(...)` are skipped, and nothing restores the input route
that `before_host_destroyed` cleared — and the toast re-fires on every subsequent changed poll.

Concretely: client A built with `DRAXUL_ENABLE_SCOREVIEW` adds a score pane; client B, an older
or leaner build, can no longer start against that Session at all.

This inverts the plan's own rule for foreign panes: "preserve them as `client_local` panes and
do not silently convert or discard". Refusing entirely is the opposite failure.

**A failed apply diverges permanently.** `TopologyClient::poll` stores the new snapshot before
returning (`libs/draxul-client/src/topology_client.cpp:65-70`), and the app only toasts on
apply failure (`app/app.cpp:3068-3073`). The next poll therefore sends the *new* revision, gets
`changed=false`, and never retries — so one transient apply failure leaves this window out of
sync with the server until an unrelated revision bump. `AgentClient` has the same shape
(`agent_client.cpp:68-73`, `app/app.cpp:3099-3105`).

**Failure toasts are unlatched**, unlike poll failures which latch via
`topology_poll_error_announced_`, so a persistent failure strobes the toast stack.

## Implementation

- [x] Project an unknown `client_host_kind` as an inert placeholder pane ("not available in
      this build") and keep the rest of the topology live. Never let it fail startup.
- [x] Make `apply_remote_topology_*` failure paths leave consistent state: restore the input
      host on every exit path, and either complete stale cleanup or roll back cleanly.
- [x] Separate "received" from "applied": keep a pending-apply revision (or let the app own
      last-applied) and retry the apply on subsequent ticks until it succeeds, rather than
      committing the revision in `poll`.
- [x] Latch apply-failure toasts on the error string, matching the poll path. (Also listed in
      `kanban/pending/17`; whichever lands first should take it.)

## Unit tests

- [x] A snapshot containing an unknown host kind still starts the client and projects every
      other pane.
- [x] A failed apply is retried on the next poll and eventually converges.
- [x] The input host is valid after a mid-application failure.
- [x] A persistent apply failure produces one toast, not one per poll.

## Acceptance criteria

- [x] A client can always start against a shared Session, whatever build produced it.
- [x] No single failed apply leaves a window permanently diverged from the server.
- [x] Full build, focused `ctest`, and smoke pass on Windows; macOS remains CI-only for
      this Windows implementation run.

## Dependencies and ownership

Touches `app/app.cpp` projection code and `libs/draxul-client`. Overlaps
`kanban/pending/27 topology-projection-extraction -refactor.md` — if `27` is scheduled first,
implement this behaviour inside the extracted projection rather than in `App`.

## Completion notes

Completed 2026-07-30. Foreign and optional-unavailable client hosts now use an inert
`UnavailableHost`; exact raw host descriptors survive reconciliation. The Session client
retains topology and agent snapshots until an epoch/revision acknowledgement, atomically
publishes command-result topology, coalesces newer revisions, invalidates old-epoch pending
state, and defers command activation until the topology has applied.

Windows Release/Ninja validation:

- production executable and both core/app test targets built;
- focused retry, placeholder, acknowledgement/coalescing, and error-latch tests passed;
- five of six parallel core/app shards passed; the existing deadline timing case in the
  remaining app shard reported `main_thread_timeout` instead of `deadline_exceeded` under
  `-j2`, then passed serially (7 assertions); and
- `py do.py smoke` rebuilt Debug/Ninja and passed the executable smoke test.

The code path is platform-neutral C++; macOS compilation and execution are left to CI because
this run was performed on Windows.
