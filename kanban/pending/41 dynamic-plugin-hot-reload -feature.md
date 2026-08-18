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

- [ ] Record the current discovery, process-lifetime cache, package staging,
      pane restart, renderer-idle, and client-local topology paths.
- [ ] Define the atomic whole-package publication contract for manifest,
      module, dependencies, shaders, and assets.
- [ ] Confirm Windows replacement avoids locking build output and macOS loads a
      distinct staged image without weakening existing symbol/Objective-C isolation.
- [ ] Define generation retention, cleanup, diagnostics, and stale-callback rules.

## Implementation and migration

- [ ] Make plugin discovery refreshable and `LoadedPlugin` generation-specific.
- [ ] Stage immutable, complete packages into app-controlled generation paths;
      prepare and validate a candidate before disrupting a pane.
- [ ] Replace the strong process-lifetime cache with active/retained generation
      ownership that unloads only after its final host reference disappears.
- [ ] Add a main-thread `PluginHost` generation swap: quiesce, release render
      pass, renderer idle, destroy, recreate, and frame request.
- [ ] Roll back to the retained prior generation when candidate creation or
      state restoration fails; show an actionable placeholder only if rollback fails.
- [ ] Add the optional versioned `draxul.hot-reload` ABI extension for bounded
      opaque state export/import; keep durable storage separate.
- [ ] Implement the extension in spinning-triangle as the reference consumer.
- [ ] Add GUI `reload_plugin` and `draxul plugin reload <id>` control surfaces,
      reloading all matching panes in the owning UI with structured results.
- [ ] Keep shared-server plugin reload client-local; do not mutate server
      topology or claim cross-client atomicity.

## Tests and validation

- [ ] Test valid candidate preparation and every loader rejection without
      changing the active generation.
- [ ] Test a real rebuilt/replaced plugin package loads a new generation from a
      distinct path while the old one is active.
- [ ] Test quiesce/render-pass release/renderer-idle/destroy ordering and stale
      callback rejection.
- [ ] Test candidate creation failure, state-import failure, successful rollback,
      and rollback failure placeholder behavior.
- [ ] Test no-extension, incompatible, invalid, oversized, and successful
      reload-state transfer paths.
- [ ] Add Vulkan and Metal reload render-smoke coverage plus shared-server
      two-client local-generation coverage.
- [ ] Run `py do.py test debug`, `py do.py smoke --skip-build`, relevant product
      scope and render checks, then `py do.py run release` for the completed slice.

## Documentation and acceptance criteria

- [ ] Document atomic package publication, hot-reload lifecycle, state-transfer
      ownership, rollback, diagnostics, and client-local scope in `docs/features.md`
      and SDK documentation.
- [ ] A malformed, partial, incompatible, or failed candidate never takes down
      a working plugin pane.
- [ ] A valid replacement can be rebuilt and activated on Windows and macOS
      without restarting Draxul or overwriting a loaded module.
- [ ] Plugins without reload-state support reload safely with a fresh instance;
      compatible plugins preserve their bounded transient state.
- [ ] Users receive unambiguous per-pane reload, rollback, or failure results.

## Dependencies and ownership

Core owns loader, host lifecycle, SDK extension, generic package staging,
control/UI routing, and tests.  Product repositories adopt the optional state
extension after the core contract is frozen.  Coordinate changes to `IHost` or
host-target boundaries with `kanban/pending/02 host-layer-static-libraries -refactor.md`.
