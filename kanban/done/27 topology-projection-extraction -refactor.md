# Extract topology projection out of App

**Type:** refactor
**Priority:** P2 / sequence 27
**Raised by:** Claude (server/client runtime review, 2026-07-30)
**Evidence:** [plans/reviews/2026-07-30-server-client-runtime-review.md](../../plans/reviews/2026-07-30-server-client-runtime-review.md#structure)

## Goal

`App` now carries two parallel mutation paths, and that fork is the direct cause of the parity
regressions in `kanban/done/23 shared-terminal-feature-parity -feature.md`. Every pane feature has to be implemented twice or it
diverges. This is the structural fix that stops that class of regression recurring.

## Observed behaviour

`app/app.cpp` is 5,419 lines. This branch added a second full mutation dispatch layer: most GUI
actions now fork on `if (topology_client_)` — the identifier appears at roughly 40 call sites.

`App` owns the entire projection state machine: the id mapping tables
(`topology_space_to_local_`, `topology_tab_to_local_`, `topology_pane_to_leaf_`,
`topology_tab_layout_signatures_`, `topology_tab_divider_nodes_`, `app/app.h:204-345`), leaf
allocation, structural signature diffing, the revision-conflict retry loop in
`execute_remote_topology_command`, and agent projection mapping. That is roughly 1,200 lines
with nothing renderer-, window-, or GPU-specific in it — exactly what CLAUDE.md says belongs in
`libs/`, not `app/`.

**The classifier is already duplicated and already divergent.** `is_remote_server_shell_kind`
(`app/app.cpp:83-95`) and `is_server_owned_shell_kind` (`app/cli_args.cpp:377-398`) answer the
same question — and are *not* identical. The `cli_args` version enumerates every `HostKind`, so
the compiler flags an addition; the `app.cpp` version ends in `default: return false`, so a
newly added shell kind is silently classified as not-server-owned. This is CLAUDE.md's
duplicate-header pitfall in function form.

**The kernel duplicates a service the same way.** `FakeTerminalRuntime` implements
`IRemoteTerminalRuntime` precisely so `RemoteTerminalService` could host it, yet the kernel
re-implements attach/poll/input/resize/take_control/disconnect with its own `FakeSubscriber`
(`libs/draxul-server/src/server_kernel.cpp:273-293`, `:1978-2203`) — and it has already
drifted: a generation mismatch is an error on the fake path (`:2033-2037`) but a snapshot
resync in the service (`remote_terminal_service.cpp:291`), and the ack-pop ordering differs.
Roughly 250 duplicated lines.

## Implementation

- [x] Extract a `TopologyProjection` class into `libs/draxul-client`, owning the id maps, leaf
      allocation, signature diffing, divider mapping, command IDs, and activation bookkeeping.
      `App` supplies only SpaceController and PaneManager callbacks; retry/acknowledgement stays
      in the worker-thread `RemoteSessionClient`.
- [x] Keep the extracted projection's command dispatch on the worker-thread client from
      `kanban/done/17 sync-ipc-on-ui-thread -bug.md`, rather than moving IPC back onto the UI
      thread.
- [x] Unify the two shell-kind classifiers into one function beside `HostKind`, enumerating
      every case so additions are compile-time errors.
- [x] Replace the kernel's hand-rolled fake-terminal path with
      `RemoteTerminalService{prefix="fake"}` over `FakeTerminalRuntime`, and delete the
      duplicate.
- [x] Collapse the `if (topology_client_)` forks: make the server-backed path the single
      mutation route behind one interface, with the local path as an implementation of it
      rather than a parallel branch.
- [x] Update `docs/module-map.md` for the new ownership.

## Unit tests

- [x] Port the existing projection assertions to test `TopologyProjection` directly, without
      an `App` — this is the main testability win.
- [x] Adding a `HostKind` without updating the classifier fails to compile.
- [x] Route-selection and parity cases cover local dispatch, every server command family,
      client-local restart fallback, host-domain classification, and unresolved identities.
- [x] The fake terminal endpoint behaves identically to before the service swap (generation
      mismatch, ack ordering, resync).
- [x] Existing `pane_manager_tests` and `space_controller_tests` still pass unchanged.

## Acceptance criteria

- [x] Projection logic is unit-testable without constructing an `App`.
- [x] One classifier, one fake-terminal state machine, one mutation route.
- [x] `app/app.cpp` is materially smaller and holds orchestration rather than protocol
      mirroring.
- [x] Windows Release/Ninja build, full `ctest`, smoke, and render snapshots pass.
- [ ] macOS full build, `ctest`, smoke, and render snapshots pass.

## Dependencies and ownership

Should follow the P0 and P1 waves — this is a large refactor and wants a stable base. Schedule
with or immediately before `kanban/done/17 sync-ipc-on-ui-thread -bug.md` (same code, and
the worker-thread move is cheaper done once) and before
`kanban/done/23 shared-terminal-feature-parity -feature.md`, so parity work lands in one
place instead of two. Related existing card:
`kanban/ice-box/22 app-tab-session-controllers -refactor.md`.

## Progress notes

The non-kernel extraction was implemented on 2026-07-30. `TopologyProjection` now owns
remote/local Space, tab, pane, leaf, signature, divider, command-ID, activation, and
apply-error bookkeeping. Retry/acknowledgement remains in the existing worker-thread
`RemoteSessionClient`. `App::project_remote_tab` is an adapter around the renderer-free
projection result and `PaneManager::reconcile_projected_layout`, rather than a protocol mirror.
Direct client-library test cases cover identity binding, stable split projection, unknown
client-host preservation, invalid graph rejection, activation revision ordering, and
classifier coverage; validation is still required before this card moves to `done`.

`ITopologyMutationRoute` now gives rename, split/duplicate, close, restart, swap, resize,
equalize, tab, and Space actions one local-identity request surface. The local adapter applies
controller mutations synchronously; the server-backed adapter resolves durable identities and
enqueues the matching `TopologyCommand`, falling back locally only for client-owned panes.
App retains lifecycle, focus, layout, persistence, and toast aftermath rather than duplicating
the mutation itself.

The server-kernel fake endpoint now delegates to
`RemoteTerminalService{method_prefix="fake"}` over `FakeTerminalRuntime`; the duplicate
subscriber, generation, sequencing, attach/poll/input/resize/control/disconnect code has been
deleted. The fake runtime publishes dirty state through the same `pump()` contract as a real
runtime. Direct endpoint coverage verifies acknowledgement ordering and generation snapshot
resync, while the existing takeover/reconnect, slow-observer resync, and large-resize burst
tests remain green. Broader cross-platform and render validation is still required before
this card moves to `done`.

Validation on Windows Release/Ninja, 2026-07-30:

- shared `draxul-tests`: 55/55 passed
- `[client][topology][projection]`: 32 assertions in 4 cases passed
- `[host_kind][topology][projection]`: 13 assertions in 1 case passed
- `[app][topology][mutation_route]`: 70 assertions in 4 cases passed
- `[server][remote-terminal][fake-service]`: 19 assertions in 1 case passed
- existing fake endpoint convergence/resync/resize parity: 191 assertions in 3 cases passed

Follow-up correlation returns exact created Space/tab/pane IDs through the bounded idempotency
cache, so concurrent clients activate the object created by their own command rather than a
snapshot-diff guess. Shared-agent restart now uses the server's durable instance ID, token
refresh, and idempotent retry path; standalone agents retain the local fallback. Restored
parent-scoped tab/pane IDs remain stable and collision-free across Spaces.
Acknowledged agent restarts fence already-published lower-generation projections until the
worker observes the new generation, so `agent.wait` cannot briefly regress to stale state.

The final Windows gate passed all 22 CTest groups, including the four core shards, two app
shards, five render comparisons, app smoke, and every optional product module. macOS
execution remains delegated to CI.
