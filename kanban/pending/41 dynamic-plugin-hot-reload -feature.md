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
- [x] Confirm Windows replacement avoids locking build output.
- [x] Confirm macOS loads a distinct staged image without weakening existing
      symbol/Objective-C isolation. The focused integration test loads v1 from
      private per-UI staging paths, publishes a same-ID v2 image, and activates
      it from a third distinct path while both old images remain resident.
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
- [x] Add Vulkan reload render-smoke coverage. The platform-neutral harness
      published the alternate shader package, reloaded one live pane, required
      a newer frame, and exported the visibly inverted Vulkan result.
- [ ] Add Metal reload render-smoke coverage. The current render-test harness
      now publishes a complete alternate-shader package after the original frame,
      reloads it, requires a post-reload frame, captures, and restores the package
      marker. The Metal test is registered but still needs execution on macOS.
- [x] Add shared-server two-client local-generation coverage. On macOS, the
      focused test reloads one UI manager from fixture v1 to rebuilt fixture v2
      while a second manager for the same pane descriptor stays interactive on v1.
- [x] Run `py do.py test debug`, `py do.py smoke --skip-build`, relevant product
      scope and render checks, then `py do.py run release` for the completed slice.

## Documentation and acceptance criteria

- [x] Document atomic package publication, hot-reload lifecycle, state-transfer
      ownership, rollback, diagnostics, and client-local scope in `docs/features.md`
      and SDK documentation.
- [x] A malformed, partial, incompatible, or failed candidate never takes down
      a working plugin pane.
- [x] A valid replacement can be rebuilt and activated on Windows without
      restarting Draxul or overwriting a loaded module.
- [x] A valid replacement can be rebuilt and activated on macOS without
      restarting Draxul or overwriting a loaded module. Evidence:
      `CCACHE_DIR=/tmp/draxul-ccache python3 do.py test debug --target
      draxul-test-app --catch '[plugin][reload][integration]'` passed all 8
      cases and 95 assertions on 2026-09-22.
- The final normal Debug validation passed all 45 unit CTest entries, startup
  smoke, and all five default Metal render comparisons.
- [x] Plugins without reload-state support reload safely with a fresh instance;
      compatible plugins preserve their bounded transient state.
- [x] Users receive an unambiguous cohort reload, rollback, or failure result.

## Windows validation (2026-09-22)

The Windows integration suite loads v1 from a UI-private staged path, replaces
the published package image with the rebuilt v2 fixture while v1 remains
resident, prepares v2 at a distinct staged path, and activates it without
restarting Draxul. It also keeps a second UI manager interactive on its resident
v1 generation. `py do.py test debug --products` passed. A deterministic
render-test hook now uses the production publisher's immutable replacement
fixture, atomically publishes it into the running app package between frames,
reloads every matching local pane, requires a newer frame, and restores the
original marker after capture. The Windows/Vulkan gate is complete; only the
macOS/Metal execution remains open above.

## Reload render-smoke validation (2026-09-22)

- The focused harness tests passed all 15 `[render]` cases and 64 assertions,
  including hook ordering, post-hook-frame gating, failure propagation,
  build-root expansion, and atomic publication-marker restoration.
- `draxul-render-spinning-triangle-reload-smoke` passed three consecutive times
  on Windows/Vulkan (2.42 s, 2.12 s, and 2.17 s). The app log reported one pane
  reloaded to a distinct runtime generation, and the exported 960x640 frame
  contains the replacement shader's inverted background and triangle colours.
- Each run restored the original application `current.json`; the replacement
  fixture generation did not remain in the live package's retained generation
  list.
- Remaining macOS gate: run `python3 do.py build debug`, then
  `ctest --test-dir build -R '^draxul-render-spinning-triangle-reload-smoke$'
  --output-on-failure -V`. Inspect
  `build/render-test-exports/spinning-triangle-reload.macos.bmp` for the inverted
  light background and multicolour triangle, then tick the Metal checkbox and
  move this card to done if the test passes without Metal validation errors.

## Dependencies and ownership

Core owns loader, host lifecycle, SDK extension, generic package staging,
control/UI routing, and tests.  Product repositories adopt the optional state
extension after the core contract is frozen.  Coordinate changes to `IHost` or
host-target boundaries with `kanban/done/02 host-layer-static-libraries -refactor.md`.
