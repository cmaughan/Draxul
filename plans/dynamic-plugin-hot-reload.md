# Dynamic plugin hot-reload plan

## Status and goal

**Status:** core implementation complete (2026-08-18); macOS and multi-client
acceptance coverage remains tracked on the kanban card. This design extends the existing trusted, client-local
native plugin system so a newly built plugin package can replace the active
implementation without restarting Draxul.  It is a core runtime seam: product
repositories consume the contract, while loader, host, control, and packaging
work belongs here.

The first supported experience is an explicit reload of every local instance
of one plugin ID.  A filesystem watcher may later report that an update is
available, but must not apply a partially built package automatically.

## Current boundary

`PluginManager` discovers manifests once at application startup and retains a
strong `LoadedPlugin` cache until the UI process exits.  `PluginHost` can
already tear down an instance safely: it quiesces the plugin, discards its
render pass, waits for the renderer to become idle, and destroys the instance.
Pane restart consequently recreates an instance from the same cached native
module rather than a rebuilt one.

This is deliberately unsuitable for replacing a live build output: Windows
locks a loaded DLL, while macOS may return an existing image for the same
`dlopen` path.  Reload must therefore load a distinct, immutable runtime
generation rather than overwrite the active image.

## Design decisions

### Package publication and staging

A producer publishes a whole plugin package atomically. It builds/copies to
`.incoming`, writes a content inventory and per-file hashes, renames the
complete immutable directory to `generations/<build-id>`, then atomically
replaces `current.json`. The build ID changes for every build even when the
semantic version does not. Existing unversioned packages are accepted as
generation zero during migration. Watchers observe only `current.json`.

Before loading, Draxul copies that complete package to an app-controlled,
unique host-private generation directory, such as
`<cache>/plugin-runtime/<plugin-id>/<generation>/`. This is separate from the
cache directory exposed to plugin code. The loader loads the native
module from that directory. Its resource directory is the matching staged
package, so code and assets cannot come from different builds.  Old staged
generations and their native handles remain resident for the manager/UI lifetime:
unloading C++/Objective-C images after TLS, static registration, or runtime
class use is not a safe general contract. Host-private runtime copies are
removed on clean manager teardown; crash residue remains ordinary recoverable
cache data. Published source packages retain the newest three generations.

Plugin-private native dependencies that may change must be statically linked
or carry generation-unique filenames/install IDs and loader-relative lookup
(`$ORIGIN`/`@loader_path`). Shadow-copying only the top-level module does not
guarantee a fresh same-named dependency image.

### Generation-aware loader

`PluginManager` keeps `LoadedPlugin` immutable and generation-specific. Its
catalog is refreshable, while a prepared candidate is independently staged,
loaded, and checked for manifest schema, ABI, ID/name/version identity, and the
active renderer backend before any pane is stopped. The publication inventory
provides build diagnostics and a future signing/integrity seam. The UI
reload coordinator checks `supported_backends` against its actual renderer.

The manager must expose a prepare operation rather than immediately replacing
the active generation.  A failed or malformed candidate cannot invalidate a
known-working pane. Loading trusted native code can still run platform module
initializers before ABI validation, so this is not a sandbox boundary. The
existing process-lifetime cache becomes an active generation record plus a
resident retired-generation list.

### Local-pane cohort replacement and rollback

Reload uses one cohort transaction for every matching pane in the owning UI,
never a live function-pointer swap:

```text
prepare + validate one candidate
  -> export optional state from every matching pane
  -> quiesce every old instance
  -> release every affected render pass
  -> renderer waits idle once; destroy every old instance
  -> create every candidate with the same pane/config/viewport/services
  -> on lifecycle failure destroy candidates and recreate every old instance
  -> otherwise import state best-effort, commit storage overlays, request frames
```

All replacement operations run on Draxul's main thread.  `quiesce_instance`
remains the plugin's promise that workers, device work, and external callbacks
have stopped before renderer-idle and destruction.  Host callbacks must reject
or harmlessly ignore requests from an instance after its reload generation has
been retired. Each instance receives a generation-scoped callback context with
an epoch and active flag instead of the reusable `PluginHost*`. Retired contexts
remain allocated until process exit and reject late callbacks safely.

If candidate loading, backend validation, instance creation, or render-pass
creation fails, Draxul recreates the retained old generation for the cohort and
reports both outcomes. State export/import is best-effort: absent, invalid,
oversized, or incompatible state starts that pane fresh and warns rather than
rolling back healthy candidate instances. If lifecycle rollback also fails,
only the affected local pane becomes the existing actionable load-failure
placeholder; shared topology is never mutated.

During candidate creation and import, the storage service uses a per-pane
overlay. Reads see durable state plus the overlay; writes/removes are journaled
and committed only after the cohort activates. Rollback drops the overlay. This
covers Draxul's storage service only; reload-aware plugins must defer raw path
writes and irreversible external device effects until activation.

### Reload-state extension

Use the existing optional-extension mechanism in C ABI v2 rather than enlarge
the base ABI table.  Define a versioned `draxul.hot-reload` extension with
bounded UTF-8 JSON export/import callbacks, a plugin-owned schema identifier and
version, an explicit size limit, and a two-call buffer contract.

State compatibility belongs to the plugin and should cover transient state
such as camera, selection, view position, and unsaved in-memory edits.  A
plugin without the extension remains reloadable but starts fresh.  Existing
plugin/pane JSON storage remains the durable persistence contract and is not
replaced by the transient handoff mechanism.

### Activation and topology

Add `reload_plugin` to the GUI actions and a `draxul plugin reload <id>`
control operation with a structured cohort result. The GUI action targets its
own UI. The CLI routes through the normal running-UI control endpoint selection.
Reloading an ID changes
every matching pane in that UI, preventing mixed generations within one
renderer process.

Plugin panes are client-local in shared-server topology.  The server retains
only the stable ID and launch JSON and does not itself load or restart client
plugins.  A future reload-everywhere command may fan out to attached UIs and
report each result, but must tolerate missing or incompatible installations.

## Vertical delivery slices

### 1. Prepared immutable generations — implemented

- Add package publication/staging support and generation metadata to the
  generic plugin registration/staging path.
- Make `PluginManager` rescan and prepare a candidate generation without
  changing the current active generation.
- Test success, duplicate/missing manifest data, missing module, bad exported
  symbol, ABI/identity mismatch, and backend incompatibility.
- Prove a replacement build is loaded from a distinct path on Windows and
  macOS, without locking the producer's output.

Review point: a bad update cannot affect a running plugin and a valid update
is independently identifiable in diagnostics.

### 2. Safe local-pane cohort swap and rollback — implemented

- Add generation-scoped callback contexts and a transactional storage overlay.
- Add `PluginHost` prepare/stop/create/activate/rollback phases and preserve pane ID, launch config,
  viewport, visibility, focus, paths, storage, and presentation plumbing.
- Coordinate every matching pane, release all render passes, and wait for the
  renderer once before destroying the old cohort.
- Add candidate-create failure and rollback integration cases with the fake
  renderer and the plugin fixture.

Review point: teardown ordering is observable and a failed update returns the
pane to its prior working generation.

### 3. State-handoff ABI extension — implemented

- Add the optional reload extension to the SDK plus header, ABI, and external
  SDK smoke coverage.
- Implement it in spinning-triangle as the reference state-transfer example.
- Test absent extension, export failure, oversized/invalid state, incompatible
  state, and successful restore. Non-success state cases keep the new lifecycle
  generation and start fresh.

Review point: state transfer is best-effort and cannot make reload unsafe.

### 4. UI and control integration — implemented

- Add focused/plugin-ID reload actions, command help, structured control
  responses, toasts, and logs.
- Coordinate all matching client-local panes in one UI and return individual
  success, rollback, or failure status.
- Document lifecycle, atomic package publication, state ownership, and the
  client-local topology boundary in `docs/features.md` and SDK documentation.

Review point: users can reliably tell what reloaded, what rolled back, and
which client owns a failure.

### 5. Developer update notification — optional follow-up

- Watch only the atomic publication marker, debounce changes, and mark a
  plugin update as available.
- Keep automatic application developer-only and opt-in; never react to raw
  compiler/linker output writes.

## Validation matrix

The Windows debug/product aggregates, reload-focused integration cases, Python
publisher test, same-cache smoke, Vulkan reference-plugin render scenario, and
Release startup gate passed on 2026-08-18. macOS/Metal and
shared-server two-client cases remain CI/follow-up acceptance work because the
reload operation is deliberately client-local and this implementation does not
add a cross-client coordinator.

- Core plugin-manager and PluginHost integration tests, including real staged
  module loading from the application package.
- Windows: rebuild while the old DLL generation is active, then load the staged
  replacement and verify the producer output was never locked by Draxul.
- macOS: prove a distinct staged dylib image is loaded while preserving current
  exported-symbol and Objective-C/ImGui isolation rules.
- Vulkan and Metal render-smoke coverage for a reloaded rendering plugin.
- Shared-server two-client coverage: one UI reloads locally while another keeps
  its installed generation; topology and server state remain intact.
- Repeated reloads prove retired callback contexts are rejected, module handles
  stay resident, and next-start cleanup removes host-private generations.
- Fixture variants with the same ID/version but different build IDs and behavior
  prove semantic version text is not generation identity.
- For each implementation slice, run the scope-appropriate aggregate test,
  same-cache smoke, and Release startup gate defined in `CLAUDE.md`.

## Non-goals

- Sandboxing or supporting untrusted native code.
- ABI compatibility with historic plugin ABI versions.
- Perfect migration of arbitrary plugin process state.
- Server-side loading of client-local native plugins.
