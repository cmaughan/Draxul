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

- [ ] Extract a `TopologyProjection` class into `libs/draxul-client`, owning the id maps, leaf
      allocation, signature diffing, and command retry. `App` supplies only SpaceController and
      PaneManager callbacks.
- [ ] Give the extracted projection the worker thread from
      `kanban/pending/17 sync-ipc-on-ui-thread -bug.md`, so the move and the threading fix land
      together rather than touching the same code twice.
- [ ] Unify the two shell-kind classifiers into one function beside `HostKind`, enumerating
      every case so additions are compile-time errors.
- [ ] Replace the kernel's hand-rolled fake-terminal path with
      `RemoteTerminalService{prefix="fake"}` over `FakeTerminalRuntime`, and delete the
      duplicate.
- [ ] Collapse the `if (topology_client_)` forks: make the server-backed path the single
      mutation route behind one interface, with the local path as an implementation of it
      rather than a parallel branch.
- [ ] Update `docs/module-map.md` for the new ownership.

## Unit tests

- [ ] Port the existing projection assertions to test `TopologyProjection` directly, without
      an `App` — this is the main testability win.
- [ ] Adding a `HostKind` without updating the classifier fails to compile.
- [ ] The fake terminal endpoint behaves identically to before the service swap (generation
      mismatch, ack ordering, resync).
- [ ] Existing `pane_manager_tests` and `space_controller_tests` still pass unchanged.

## Acceptance criteria

- [ ] Projection logic is unit-testable without constructing an `App`.
- [ ] One classifier, one fake-terminal state machine, one mutation route.
- [ ] `app/app.cpp` is materially smaller and holds orchestration rather than protocol
      mirroring.
- [ ] Full build, `ctest`, smoke, and the render snapshot suite pass on both platforms.

## Dependencies and ownership

Should follow the P0 and P1 waves — this is a large refactor and wants a stable base. Schedule
with or immediately before `kanban/pending/17 sync-ipc-on-ui-thread -bug.md` (same code, and
the worker-thread move is cheaper done once) and before
`kanban/done/23 shared-terminal-feature-parity -feature.md`, so parity work lands in one
place instead of two. Related existing card:
`kanban/ice-box/22 app-tab-session-controllers -refactor.md`.
