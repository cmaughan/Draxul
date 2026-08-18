# Add dynamic plugin hot reload

**Type:** feature
**Priority:** P1 / sequence 41
**Raised by:** GPT/Codex
**Plan:** `plans/dynamic-plugin-hot-reload.md`

## Goal

Allow an explicitly requested, newly published native plugin package to replace
the active local plugin implementation without restarting Draxul, while keeping
the old generation live until a validated replacement is ready and restoring it
when the replacement cannot start.

## Boundary verification

- [x] Record the current discovery, process-lifetime cache, package staging,
      pane restart, renderer-idle, and client-local topology paths.
- [x] Define the atomic whole-package publication contract for manifest,
      module, dependencies, shaders, and assets.
- [ ] Confirm Windows replacement avoids locking build output and macOS loads a
      distinct staged image without weakening existing symbol/Objective-C isolation.
- [x] Define generation retention, cleanup, diagnostics, and stale-callback rules.

## Implementation and migration

- [x] Make plugin discovery refreshable and `LoadedPlugin` generation-specific.
- [x] Stage immutable, complete packages into app-controlled generation paths;
      prepare and validate a candidate before disrupting a pane.
- [x] Keep active and retired generation modules resident for the UI manager
      lifetime and clean host-private staged directories on clean teardown.
- [x] Add a main-thread cohort swap: export, quiesce all, release all passes,
      one renderer idle, destroy all, recreate all, activate or roll back all.
- [x] Give every instance a durable generation-scoped callback context so stale
      callbacks cannot target a replacement or freed host.
- [x] Journal storage-service writes/removes during candidate creation and commit
      only after cohort activation; discard them on lifecycle rollback.
- [x] Roll back the cohort when candidate lifecycle creation fails; treat state
      export/import failure as a fresh-state warning, not lifecycle failure.
- [x] Add the optional versioned `draxul.hot-reload` ABI extension for bounded
      opaque state export/import; keep durable storage separate.
- [x] Implement the extension in spinning-triangle as the reference consumer.
- [x] Add GUI `reload_plugin` and `draxul plugin reload <id>` control surfaces,
      reloading all matching panes in the owning UI with structured results.
- [x] Keep shared-server plugin reload client-local; do not mutate server
      topology or claim cross-client atomicity.

## Tests and validation

- [x] Test valid candidate preparation and loader rejection without
      changing the active generation.
- [x] Test a real rebuilt/replaced plugin package loads a new generation from a
      distinct path while the old one is active.
- [x] Test quiesce/render-pass release/renderer-idle/destroy ordering and stale
      callback rejection.
- [x] Test candidate creation failure, best-effort state-import failure, and
      successful rollback; keep rollback-failure placeholder coverage pending.
- [x] Test no-extension, incompatible, invalid, oversized, and successful
      reload-state transfer paths.
- [ ] Add Vulkan and Metal reload render-smoke coverage plus shared-server
      two-client local-generation coverage.
- [x] Run `py do.py test debug`, `py do.py smoke --skip-build`, relevant product
      scope and render checks, then `py do.py run release` for the completed slice.

## Documentation and acceptance criteria

- [x] Document atomic package publication, hot-reload lifecycle, state-transfer
      ownership, rollback, diagnostics, and client-local scope in `docs/features.md`
      and SDK documentation.
- [x] A malformed, partial, incompatible, or failed candidate never takes down
      a working plugin pane.
- [ ] A valid replacement can be rebuilt and activated on Windows and macOS
      without restarting Draxul or overwriting a loaded module.
- [x] Plugins without reload-state support reload safely with a fresh instance;
      compatible plugins preserve their bounded transient state.
- [x] Users receive an unambiguous cohort reload, rollback, or failure result.

## Dependencies and ownership

Core owns loader, host lifecycle, SDK extension, generic package staging,
control/UI routing, and tests.  Product repositories adopt the optional state
extension after the core contract is frozen.  Coordinate changes to `IHost` or
host-target boundaries with `kanban/pending/02 host-layer-static-libraries -refactor.md`.
