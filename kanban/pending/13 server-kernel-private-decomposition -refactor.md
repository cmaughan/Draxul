# Decompose `ServerKernel::Impl` behind its façade

**Type:** refactor  
**Priority:** P1  
**Raised by:** Claude, Codex, and Grok  
**Depends on:** card `12`; pending `09`, `10`, and `14` completion/freeze

## Boundary verification

- [ ] Inventory all `Impl` state and methods by lifecycle, clients, sessions, requests, terminals/agents, and event loop.
- [ ] Record lock, task, generation, lease, cache, and shutdown invariants.
- [ ] Inventory public `ServerAgentService` consumers.
- [ ] Partition the 4,928-line test suite without losing tags/cases.

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
- [ ] Build `draxul-server` and `draxul-test-core`; run CTest label `core`.
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
