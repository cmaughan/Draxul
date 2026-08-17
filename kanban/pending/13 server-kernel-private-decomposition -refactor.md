# Decompose `ServerKernel::Impl` behind its façade

**Type:** refactor  
**Priority:** P1  
**Raised by:** Claude, Codex, and Grok  
**Depends on:** `kanban/pending/12 control-transport-boundary -refactor.md`.
The earlier lifecycle prerequisites are complete in
`kanban/done/09 macos-remote-terminal-channel -bug.md`,
`kanban/done/10 server-lifecycle-sigterm-eviction -bug.md`, and
`kanban/done/14 server-agent-runtime-control -feature.md`.

## Boundary verification

- [ ] Inventory all `Impl` state and methods by lifecycle, clients, sessions, requests, terminals/agents, and event loop.
- [ ] Record lock, task, generation, lease, cache, and shutdown invariants.
- [ ] Inventory public `ServerAgentService` consumers.
- [ ] Partition the current server-kernel test suite without losing tags/cases.

## Implementation and migration

- [ ] Add private `server_kernel_impl.h`.
- [ ] Move member definitions mechanically into responsibility TUs.
- [ ] Move `ServerAgentService` to `src/`.
- [ ] Add `draxul-server-test-internals`.
- [ ] Split tests by responsibility.
- [ ] Introduce registry/store collaborators only after state ownership is proven.
- [ ] Do not add service static libraries or another mutex.

## Unit tests

- [ ] Add focused lifecycle/discovery, client/authentication, checkpoint, topology/terminal, agent, and resource suites.
- [ ] Build `draxul-server` and the owning core test target; run the focused CTest selection.
- [ ] Preserve the server public-header link-isolation build.

## Cross-platform validation

- [ ] Validate publication, process identity, eviction, signals, and terminal lifecycle on Windows and macOS.
- [ ] Confirm the server remains renderer/window/host/product free.

## Agent documentation/tooling

- [ ] Document private state/lock ownership beside the implementation.
- [ ] Update the server row in `docs/module-map.md`.

## Acceptance criteria

- [ ] `ServerKernel` public API is unchanged.
- [ ] No implementation TU owns unrelated method families.
- [ ] `ServerAgentService` is no longer production-public.
- [ ] Focused/full tests and headless smoke remain green.
