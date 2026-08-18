# Dynamic plugin hot-reload plan

## Status and goal

**Status:** proposed.  This plan extends the existing trusted, client-local
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

A producer publishes a whole plugin package atomically.  It builds/copies to a
temporary directory, then atomically replaces a versioned package directory or
updates a small current-generation metadata file only once the manifest,
library, dependent native libraries, shaders, and assets are complete.

Before loading, Draxul copies that complete package to an app-controlled,
unique generation directory, such as
`<cache>/plugins/<plugin-id>/<generation>/`.  The loader loads the native
module from that directory.  Its resource directory is the matching staged
package, so code and assets cannot come from different builds.  Old staged
generations remain until no host references them, then are pruned with a
bounded retention policy on startup and after successful reloads.

### Generation-aware loader

`PluginManager` keeps `LoadedPlugin` immutable and generation-specific.  Its
catalog is refreshable, while a prepared candidate is independently staged,
loaded, and checked for manifest schema, ABI, ID/name/version identity, and
the active renderer backend before any pane is stopped.

The manager must expose a prepare operation rather than immediately replacing
the active generation.  A failed or malformed candidate cannot invalidate a
known-working module.  The existing process-lifetime cache becomes an active
generation record plus retained `shared_ptr` generations; a generation is
unloaded only when its final host reference is released.

### Per-pane replacement and rollback

Reload uses a controlled instance swap, never a live function-pointer swap:

```text
prepare + validate candidate
  -> export optional reload state
  -> quiesce old instance
  -> release render pass; renderer waits idle; destroy old instance
  -> create candidate with same pane/config/viewport/services
  -> restore optional state; request a frame
  -> activate, or recreate the retained old generation on failure
```

All replacement operations run on Draxul's main thread.  `quiesce_instance`
remains the plugin's promise that workers, device work, and external callbacks
have stopped before renderer-idle and destruction.  Host callbacks must reject
or harmlessly ignore requests from an instance after its reload generation has
been retired.

If candidate creation or state import fails, Draxul recreates the retained old
generation and reports both candidate and rollback outcomes.  If rollback also
fails, only that local pane becomes the existing actionable load-failure
placeholder; shared topology is never mutated.

### Reload-state extension

Use the existing optional-extension mechanism in C ABI v2 rather than enlarge
the base ABI table.  Define a versioned `draxul.hot-reload` extension with
bounded, opaque state export/import callbacks; JSON is the initial wire format.
The extension has explicit size limits and a two-call buffer contract.

State compatibility belongs to the plugin and should cover transient state
such as camera, selection, view position, and unsaved in-memory edits.  A
plugin without the extension remains reloadable but starts fresh.  Existing
plugin/pane JSON storage remains the durable persistence contract and is not
replaced by the transient handoff mechanism.

### Activation and topology

Add `reload_plugin` to the GUI actions and a `draxul plugin reload <id>`
control operation with structured per-pane results.  In the initial release,
reloading an ID reloads every matching pane in the owning UI, preventing mixed
generations of the same plugin within one renderer process.

Plugin panes are client-local in shared-server topology.  The server retains
only the stable ID and launch JSON and does not itself load or restart client
plugins.  A future reload-everywhere command may fan out to attached UIs and
report each result, but must tolerate missing or incompatible installations.

## Vertical delivery slices

### 1. Prepared immutable generations

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

### 2. Safe single-pane swap and rollback

- Add the `PluginHost` reload operation and preserve pane ID, launch config,
  viewport, visibility, focus, paths, storage, and presentation plumbing.
- Reuse the shutdown ordering; make stale callback rejection explicit.
- Add candidate-create failure and rollback integration cases with the fake
  renderer and the plugin fixture.

Review point: teardown ordering is observable and a failed update returns the
pane to its prior working generation.

### 3. State-handoff ABI extension

- Add the optional reload extension to the SDK plus header, ABI, and external
  SDK smoke coverage.
- Implement it in spinning-triangle as the reference state-transfer example.
- Test absent extension, oversized/invalid state, incompatible state, and a
  successful restore.

Review point: state transfer is best-effort and cannot make reload unsafe.

### 4. UI and control integration

- Add focused/plugin-ID reload actions, command help, structured control
  responses, toasts, and logs.
- Coordinate all matching client-local panes in one UI and return individual
  success, rollback, or failure status.
- Document lifecycle, atomic package publication, state ownership, and the
  client-local topology boundary in `docs/features.md` and SDK documentation.

Review point: users can reliably tell what reloaded, what rolled back, and
which client owns a failure.

### 5. Developer update notification (optional follow-up)

- Watch only the atomic publication marker, debounce changes, and mark a
  plugin update as available.
- Keep automatic application developer-only and opt-in; never react to raw
  compiler/linker output writes.

## Validation matrix

- Core plugin-manager and PluginHost integration tests, including real staged
  module loading from the application package.
- Windows: rebuild while the old DLL generation is active, then load the staged
  replacement and verify the producer output was never locked by Draxul.
- macOS: prove a distinct staged dylib image is loaded while preserving current
  exported-symbol and Objective-C/ImGui isolation rules.
- Vulkan and Metal render-smoke coverage for a reloaded rendering plugin.
- Shared-server two-client coverage: one UI reloads locally while another keeps
  its installed generation; topology and server state remain intact.
- For each implementation slice, run the scope-appropriate aggregate test,
  same-cache smoke, and Release startup gate defined in `CLAUDE.md`.

## Non-goals

- Sandboxing or supporting untrusted native code.
- ABI compatibility with historic plugin ABI versions.
- Perfect migration of arbitrary plugin process state.
- Server-side loading of client-local native plugins.
