# Split host API, grid host, terminal core, and terminal host

**Type:** refactor
**Priority:** P1 / sequence 02
**Raised by:** GPT/Codex
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 3

## Goal

Replace the broad `draxul-host` implementation target with incremental host API,
grid-host, terminal-core, remote-terminal presentation, and Nvim boundaries.
Shell process ownership now belongs exclusively to the headless server.

## Boundary verification

- [ ] Classify every `libs/draxul-host` public header/source and every direct consumer.
- [ ] Verify actual public-header dependency needs for `host.h`, `host_registry.h`,
  `grid_host_base.h`, terminal state headers, and concrete host headers.
- [ ] Map pure terminal tests separately from client presentation and
  server-owned process/runtime tests.
- [ ] Record `IHost`, callback, provider, focus, shutdown, and factory lifetime invariants.
- [ ] Reconcile links with current optional modules and link-isolation targets.

## Implementation and migration

- [ ] Add `draxul-host-api` for neutral contracts and provider registry; keep include paths/namespaces stable.
- [ ] Make `draxul-host` a compatibility aggregate and migrate non-grid product hosts to the API target.
- [ ] Add `draxul-grid-host` for `GridHostBase` and migrate real grid consumers.
- [ ] Add `draxul-terminal-core` for VT/SGR/scrollback/selection/alternate-screen/mouse/key logic.
- [ ] Split client terminal presentation (`RemoteTerminalHost`, selection,
  copy-mode, rendering adapters) from Nvim host integration.
- [ ] Keep ConPTY and Unix PTY implementation in `draxul-terminal-process`,
  linked only by `draxul-server`; do not recreate client-local shell factories.
- [ ] Remove the compatibility aggregate only after all consumers migrate and each phase is green.

## Unit tests

- [ ] Add public-header/link-isolation consumers for `draxul-host-api` and `draxul-terminal-core`.
- [ ] Link registry/provider tests to host API only.
- [ ] Link VT, SGR, scrollback, selection, mouse, and key-encoding tests to terminal core only.
- [ ] Keep grid cursor/render tests on `draxul-grid-host`, remote presentation
  tests on the client host target, and lifecycle/process tests on
  `draxul-terminal-process`/`draxul-server`.
- [ ] Build `draxul-test-core` plus affected product test targets after each phase.

## Cross-platform validation

- [ ] Windows: exercise server-owned ConPTY launch, remote presentation, Nvim,
  and non-blocking shutdown.
- [ ] macOS: exercise server-owned Unix PTY/fork-exec launch, remote
  presentation, and deterministic shutdown.
- [ ] Build all optional product hosts against the narrow API/grid boundary.
- [ ] Confirm the split changes no Vulkan/Metal scene or backend contract.
- [ ] Run full `ctest` and smoke on each available platform; record unavailable runtime coverage.

## Agent documentation and tooling

- [ ] Update `docs/module-map.md` with the new target direction and compatibility period.
- [ ] Correct canonical host architecture guidance after the final phase lands.
- [ ] Expose focused build/test target names through the label tooling card.

## Acceptance criteria

- [ ] Non-grid products do not inherit terminal, PTY, or Nvim implementation dependencies.
- [ ] Pure terminal tests build without concrete platform hosts or Nvim processes.
- [ ] Existing header paths, `IHost` behavior, provider registration, and callbacks remain compatible.
- [ ] Each target has minimal declared PUBLIC dependencies and passes link isolation.
- [ ] Full build, tests, optional ON/OFF configurations, and smoke pass.

## Dependencies and ownership

Depends on `kanban/pending/00 internal-target-build-policy -refactor.md`. One
host-architecture owner controls public interfaces and compatibility migration.
After those interfaces freeze, terminal-core tests and platform-host moves may be
assigned independently. Coordinate final Nvim links with
`kanban/pending/03 nvim-protocol-transport-boundaries -refactor.md`; blocks the
Kanban host split in `kanban/pending/04 kanban-core-host-boundary -refactor.md`.
