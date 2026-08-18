# Split host API, grid host, terminal core, and terminal host

**Type:** refactor
**Priority:** P1 / sequence 02
**Raised by:** GPT/Codex
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 3

## Goal

Replace the broad `draxul-host` implementation target with incremental host API,
grid-host, terminal-core, remote-terminal presentation, and Nvim boundaries.
Shell process ownership now belongs exclusively to the headless server.

## Delivered checkpoint

`draxul-terminal-core` and `draxul-terminal-process` now provide the renderer-free
terminal state and server-only PTY/ConPTY boundaries. Link-isolation coverage and
configure-time dependency checks prevent either target from reaching host, window,
renderer, SDL, or product code. Product hosts subsequently moved behind the native
plugin ABI. The remaining scope is the host API/grid/presentation/Nvim split.

## Boundary verification

- [ ] Classify every `libs/draxul-host` public header/source and every direct consumer.
- [ ] Verify actual public-header dependency needs for `host.h`, `host_registry.h`,
  `grid_host_base.h`, terminal state headers, and concrete host headers.
- [ ] Map pure terminal tests separately from client presentation and
  server-owned process/runtime tests.
- [ ] Record `IHost`, callback, provider, focus, shutdown, and factory lifetime invariants.
- [ ] Reconcile links with built-in Markdown/Kanban hosts and current link-isolation targets.

## Implementation and migration

- [ ] Add `draxul-host-api` for neutral contracts and provider registry; keep include paths/namespaces stable.
- [ ] Make `draxul-host` a compatibility aggregate and migrate non-grid product hosts to the API target.
- [ ] Add `draxul-grid-host` for `GridHostBase` and migrate real grid consumers.
- [x] Add `draxul-terminal-core` for VT/SGR/scrollback/selection/alternate-screen/mouse/key logic.
- [ ] Split client terminal presentation (`RemoteTerminalHost`, selection,
  copy-mode, rendering adapters) from Nvim host integration.
- [x] Keep ConPTY and Unix PTY implementation in `draxul-terminal-process`,
  linked only by `draxul-server`; do not recreate client-local shell factories.
- [ ] Remove the compatibility aggregate only after all consumers migrate and each phase is green.

## Unit tests

- [ ] Add a public-header/link-isolation consumer for `draxul-host-api`.
- [x] Keep the existing public-header/link-isolation consumer for `draxul-terminal-core`.
- [ ] Link registry/provider tests to host API only.
- [x] Link VT, SGR, scrollback, selection, mouse, and key-encoding tests to terminal core only.
- [ ] Keep grid cursor/render tests on the future `draxul-grid-host`, remote presentation
  tests on the client host target, and lifecycle/process tests on
  `draxul-terminal-process`/`draxul-server`.
- [ ] Build the owning core test aggregate plus affected built-in host tests after each phase.

## Cross-platform validation

- [ ] Windows: exercise server-owned ConPTY launch, remote presentation, Nvim,
  and non-blocking shutdown.
- [ ] macOS: exercise server-owned Unix PTY/fork-exec launch, remote
  presentation, and deterministic shutdown.
- [x] External products use the native plugin ABI and do not consume the host target.
- [ ] Confirm the split changes no Vulkan/Metal scene or backend contract.
- [ ] Run full `ctest` and smoke on each available platform; record unavailable runtime coverage.

## Agent documentation and tooling

- [x] Update `docs/module-map.md` with the delivered terminal target direction.
- [ ] Update it again for the host API/grid split and compatibility period.
- [ ] Correct canonical host architecture guidance after the final phase lands.
- [ ] Expose focused build/test target names through the label tooling card.

## Acceptance criteria

- [x] External products do not inherit terminal, PTY, or Nvim implementation dependencies.
- [x] Pure terminal tests build without concrete platform hosts or Nvim processes.
- [ ] Existing header paths, `IHost` behavior, provider registration, and callbacks remain compatible.
- [ ] Each target has minimal declared PUBLIC dependencies and passes link isolation.
- [ ] Full core build/tests, mounted-plugin availability configurations, and smoke pass.

## Dependencies and ownership

Depends on `kanban/pending/00 internal-target-build-policy -refactor.md`. One
host-architecture owner controls public interfaces and compatibility migration.
After those interfaces freeze, terminal-core tests and platform-host moves may be
assigned independently. Coordinate final Nvim links with
`kanban/pending/03 nvim-protocol-transport-boundaries -refactor.md`; blocks the
Kanban host split in `kanban/pending/04 kanban-core-host-boundary -refactor.md`.
