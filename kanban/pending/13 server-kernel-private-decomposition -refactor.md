# Decompose `ServerKernel::Impl` behind its façade

**Type:** refactor
**Priority:** P1
**Raised by:** Claude, Codex, and Grok
**Depends on:** `kanban/pending/12 control-transport-boundary -refactor.md`.
The earlier lifecycle prerequisites are complete in
`kanban/done/09 macos-remote-terminal-channel -bug.md`,
`kanban/done/10 server-lifecycle-sigterm-eviction -bug.md`, and
`kanban/done/14 server-agent-runtime-control -feature.md`.

**Downstream:** the persistent endpoint and bounded per-UI writer ownership in
`kanban/pending/40 multiplexed-session-event-stream -feature.md` wait for this
decomposition. This card does not implement Session streaming.

## Boundary verification

- [x] Inventory all `Impl` state and methods by lifecycle, clients, sessions, requests, terminals/agents, and event loop.
- [x] Record lock, task, generation, lease, cache, and shutdown invariants.
- [x] Inventory public `ServerAgentService` consumers.
- [x] Partition the current server-kernel test suite without losing tags/cases.

## Implementation and migration

- [x] Add private `server_kernel_impl.h`.
- [x] Move member definitions mechanically into responsibility TUs.
- [x] Move `ServerAgentService` to `src/`.
- [x] Add `draxul-server-test-internals`.
- [x] Split tests by responsibility.
- [x] Keep registry/store collaborators deferred; the state thread retains ownership.
- [x] Do not add service static libraries or another mutex.

### Delivered checkpoint — private implementation boundary

`libs/draxul-server/src/server_kernel_impl.h` now holds the complete private state
and method inventory grouped by lifecycle, request/client leases, Session
persistence, and terminal/agent ownership. It records the single kernel mutex
boundary, state-thread ownership, checkpoint-task handoff, wake edges, generation
and revision gates, bounded mutation cache, and shutdown lifetime invariants beside
the fields they govern. The public façade definitions moved mechanically to
`server_kernel_facade.cpp`; `server_kernel.h` and runtime behavior are unchanged.

`ServerAgentService` is now a private collaborator under `src/`, and the two
white-box suites that construct it opt into the explicit
`draxul-server-test-internals` include/link boundary. The installed/public include
surface no longer exposes the service.

The former 66-case `tests/server_kernel_tests.cpp` suite is now partitioned into
service/protocol, lifecycle/discovery, client/authentication, Session/checkpoint,
topology/terminal, agent-runtime, and process-integration suites sharing only
`tests/support/server_kernel_test_support.h`. A static inventory comparison preserves
every original case title and tag while making responsibility drift visible in the
source layout.

The final macOS Debug gate rebuilt the server and its public-header link-isolation
target, preserved the configured static test inventory, passed the product-scoped
aggregate (39/39), and passed the same-cache headless smoke (one scenario in
6.55 seconds). Windows validation remains outstanding.

### Delivered checkpoint — responsibility translation units

The private boundary now supports behavior-preserving compilation units for the
public façade (`server_kernel_facade.cpp`), authenticated client leases and service
detachment (`server_kernel_clients.cpp`), startup/publication/state-loop/shutdown
(`server_kernel_lifecycle.cpp`), and authenticated method dispatch
(`server_kernel_requests.cpp`). These files share the existing `Impl` state and do
not introduce collaborators, locks, protocol changes, or new runtime ownership.
Session restore, lookup, persistence, and status definitions now live in
`server_kernel_sessions.cpp`. Terminal and managed-agent runtime ownership lives
in `server_kernel_terminals.cpp`. The move preserves the existing `Impl` state,
single kernel mutex boundary, public façade, and runtime behavior; it adds no
service library or collaborator.

## Unit tests

- [x] Add focused lifecycle/discovery, client/authentication, checkpoint,
      topology/terminal, agent-runtime, service/protocol, and process-integration suites.
- [x] Build `draxul-server` and the owning core test target; run the aggregate CTest selection.
- [x] Preserve the server public-header link-isolation build.

## Cross-platform validation

- [x] Validate publication, process identity, eviction, signals, and terminal lifecycle on macOS.
- [ ] Validate the same behavior on Windows.
- [x] Confirm the server remains renderer/window/host/product free.

## Agent documentation/tooling

- [x] Document private state/lock ownership beside the implementation.
- [x] Update the server row in `docs/module-map.md`.

## Acceptance criteria

- [x] `ServerKernel` public API is unchanged.
- [x] No implementation TU owns unrelated method families.
- [x] `ServerAgentService` is no longer production-public.
- [x] Focused/full tests and headless smoke remain green.
