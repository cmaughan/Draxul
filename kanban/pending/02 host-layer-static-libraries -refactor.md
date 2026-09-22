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

- [x] Classify every `libs/draxul-host` public header/source and every direct consumer.
- [x] Verify actual public-header dependency needs for `host.h`, `host_registry.h`,
  `grid_host_base.h`, terminal state headers, and concrete host headers.
- [x] Map pure terminal tests separately from client presentation and
  server-owned process/runtime tests.
- [x] Record `IHost`, callback, provider, focus, shutdown, and factory lifetime invariants.
- [x] Reconcile links with built-in Markdown/Kanban hosts and current link-isolation targets.

## Implementation and migration

- [x] Add `draxul-host-api` for neutral contracts and provider registry; keep include paths/namespaces stable.
- [x] Make `draxul-host` a compatibility aggregate and migrate non-grid product hosts to the API target.
- [x] Add `draxul-grid-host` for `GridHostBase` and migrate real grid consumers.
- [x] Add `draxul-terminal-core` for VT/SGR/scrollback/selection/alternate-screen/mouse/key logic.
- [x] Split client terminal presentation (`RemoteTerminalHost`, selection,
  copy-mode, rendering adapters) from Nvim host integration.
- [x] Keep ConPTY and Unix PTY implementation in `draxul-terminal-process`,
  linked only by `draxul-server`; do not recreate client-local shell factories.
- [x] Remove the compatibility aggregate after all consumers migrate and the leaf-target build/test phase is green.

## Unit tests

- [x] Add a public-header/link-isolation consumer for `draxul-host-api`.
- [x] Keep the existing public-header/link-isolation consumer for `draxul-terminal-core`.
- [x] Link registry/provider tests to host API only.
- [x] Link VT, SGR, scrollback, selection, mouse, and key-encoding tests to terminal core only.
- [x] Keep grid cursor/render tests on `draxul-grid-host`, remote presentation
  tests on the client host target, and lifecycle/process tests on
  `draxul-terminal-process`/`draxul-server`.
- [x] Build the owning core test aggregate plus affected built-in host tests after each phase.

## Cross-platform validation

- [ ] Windows: exercise server-owned ConPTY launch, remote presentation, Nvim,
  and non-blocking shutdown.
- [x] macOS: exercise server-owned Unix PTY/fork-exec launch, remote
  presentation, and deterministic shutdown.
- [x] External products use the native plugin ABI and do not consume the host target.
- [x] Confirm the split changes no Vulkan/Metal scene or backend contract.
- [x] Run full `ctest` and smoke on each available platform; record unavailable runtime coverage.

## Agent documentation and tooling

- [x] Update `docs/module-map.md` with the delivered terminal target direction.
- [x] Update it again for the host API/grid split and compatibility period.
- [x] Correct canonical host architecture guidance after the final phase lands.
- [x] Expose focused build/test target names through the label tooling card.

## Acceptance criteria

- [x] External products do not inherit terminal, PTY, or Nvim implementation dependencies.
- [x] Pure terminal tests build without concrete platform hosts or Nvim processes.
- [x] Existing header paths, `IHost` behavior, provider registration, and callbacks remain compatible.
- [x] Each target has minimal declared PUBLIC dependencies and passes link isolation.
- [x] Full core build/tests, mounted-plugin availability configurations, and smoke pass.

## Validation record

- Apple Clang Debug full `draxul` build passed with all mounted products enabled.
- Focused `app-shell|host-api` CTest selection passed 3/3.
- Core aggregate passed 14/14 outside the filesystem sandbox; the sandboxed run
  could not bind the tests' local Unix control sockets.
- Same-cache Metal startup smoke passed. Windows runtime coverage was unavailable
  in this workspace; the configure-time dependency audit and cross-platform source
  selection remain active.
- Removed the final `draxul-host` compatibility interface after migrating the
  MegaCity and SatView test aggregates to `draxul-host-api`. MegaCity now declares
  its `draxul-config` test dependency directly instead of receiving it through the
  broad aggregate. The leaf host targets plus both product test executables built,
  and the host API, MegaCity, and SatView focused shards passed 5/5 on macOS.
- The final elevated core plus Rezonality aggregate passed 30/30 after the
  compatibility removal, followed by a same-cache Metal startup smoke.
- Product checkpoints: MegaCity `9192341`; SatView `034d757`.

## Dependencies and ownership

Depends on `kanban/pending/00 internal-target-build-policy -refactor.md`. One
host-architecture owner controls public interfaces and compatibility migration.
After those interfaces freeze, terminal-core tests and platform-host moves may be
assigned independently. Coordinate final Nvim links with
`kanban/pending/03 nvim-protocol-transport-boundaries -refactor.md`; blocks the
Kanban host split in `kanban/pending/04 kanban-core-host-boundary -refactor.md`.
