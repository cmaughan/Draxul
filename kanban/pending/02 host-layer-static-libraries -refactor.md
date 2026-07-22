# Split host API, grid host, terminal core, and terminal host

**Type:** refactor
**Priority:** P1 / sequence 02
**Raised by:** GPT/Codex
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 3

## Goal

Replace the single broad `draxul-host` implementation target with incremental
host API, grid, terminal-core, and platform-host boundaries while keeping source
and ABI compatibility during migration.

## Boundary verification

- [ ] Classify every `libs/draxul-host` public header/source and every direct consumer.
- [ ] Verify actual public-header dependency needs for `host.h`, `host_registry.h`,
  `grid_host_base.h`, terminal state headers, and concrete host headers.
- [ ] Map pure terminal tests separately from grid-host and platform-process tests.
- [ ] Record `IHost`, callback, provider, focus, shutdown, and factory lifetime invariants.
- [ ] Reconcile links with current optional modules and link-isolation targets.

## Implementation and migration

- [ ] Add `draxul-host-api` for neutral contracts and provider registry; keep include paths/namespaces stable.
- [ ] Make `draxul-host` a compatibility aggregate and migrate non-grid product hosts to the API target.
- [ ] Add `draxul-grid-host` for `GridHostBase` and migrate real grid consumers.
- [ ] Add `draxul-terminal-core` for VT/SGR/scrollback/selection/alternate-screen/mouse/key logic.
- [ ] Add `draxul-terminal-host` for terminal bases, Nvim/shell factories, and platform PTY sources.
- [ ] Keep ConPTY and Unix PTY selected in the same logical target by platform.
- [ ] Remove the compatibility aggregate only after all consumers migrate and each phase is green.

## Unit tests

- [ ] Add public-header/link-isolation consumers for `draxul-host-api` and `draxul-terminal-core`.
- [ ] Link registry/provider tests to host API only.
- [ ] Link VT, SGR, scrollback, selection, mouse, and key-encoding tests to terminal core only.
- [ ] Keep grid cursor/render tests on `draxul-grid-host` and lifecycle/process tests on terminal host.
- [ ] Build `draxul-test-core` plus affected product test targets after each phase.

## Cross-platform validation

- [ ] Windows: exercise ConPTY/shell/Nvim launch and non-blocking shutdown.
- [ ] macOS: exercise Unix PTY/fork-exec launch and deterministic shutdown.
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
