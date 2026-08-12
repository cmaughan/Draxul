# Dynamic Plugin Migration for SatView and ScoreView

Status: active architecture plan
Recorded: 2026-08-11

Both product hosts can move to the dynamic plugin architecture, but they should
not simply be compiled into DLLs or macOS modules in their current `IHost` form.
Doing that would expose Draxul's C++ renderer, ImGui, SDL, GLM, allocator, and CRT
details across the plugin boundary.

The intended result is:

- `dev.draxul.satview`: primarily a native GPU plugin.
- `dev.draxul.scoreview`: a backend-neutral 2D plugin with private engraving and
  audio workers.
- `PluginHost` remains the only Draxul `IHost` used by these products.
- The server continues storing only the plugin ID plus bounded launch
  configuration.
- A whole-tab plugin is a tab containing one plugin pane; it requires no separate
  tab-rendering ABI.

## Compatibility policy

Draxul has no external plugin users yet, so this migration should make a clean
pre-release break instead of carrying compatibility machinery forward.

- Replace or reshape the current ABI wherever that produces a clearer design.
- Bump the ABI to v2 so stale DLLs/modules fail explicitly rather than being
  mistaken for current plugins.
- Update the spinning-triangle plugin, fixture modules, manifests, SDK headers,
  tests, and documentation atomically with the host.
- Do not implement v1 shims, dual callback tables, fallback loading, or other
  code whose only purpose is running plugins built against the current ABI.
- Version individual optional services so their contracts are unambiguous, not
  to preserve the pre-release ABI indefinitely.

## ABI additions

Replace the existing ABI v1 tables in
`libs/draxul-plugin/include/draxul/plugin_api.h` with a coherent current-only
ABI v2. Add two generic extension mechanisms:

```c
// Plugin asks Draxul for an optional host service.
query_host_service(host_context, service_id, minimum_version, out_table, out_size);

// Draxul asks the plugin for an optional capability.
query_plugin_extension(instance, extension_id, minimum_version);
```

Every returned service has its own version and `struct_size`. This keeps the
base ABI small, makes optional capabilities independently testable, and gives
future evolution a clear boundary. The loader accepts only the current module
ABI during this pre-release period.

### 1. Lifecycle and ticking

```c
tick(instance, now, visible, focused)
    -> next_tick_delay, request_redraw

quiesce_instance(instance)
```

Rendering and background work need different deadlines:

- Render deadlines disappear while hidden.
- A plugin can optionally retain a logic deadline while hidden.
- Worker threads call a thread-safe `request_tick`, which wakes the main loop
  without necessarily rendering.
- `quiesce_instance` stops workers, audio, and device callbacks before renderer
  idle and destruction.

This replaces polling. SatView can coalesce simulation results while hidden;
ScoreView can either pause on hide or explicitly continue playback.

### 2. Presentation and chrome extension

This supplies the `IHost` information currently missing from plugins:

- Display name and status text.
- Default background.
- Content-ready/error state.
- Print crop and paper-white hint.
- Cursor shape.
- Named actions and action dispatch.
- A `presentation_changed` notification.

ScoreView currently depends on its print hint, so this is part of behavioral
parity rather than optional polish.

### 3. Local storage and paths

Add a host service providing:

- Atomic JSON read/write, scoped by plugin ID.
- Optional per-instance or per-pane storage.
- User data and cache directories.
- Temporary directory access.
- The plugin resource directory is already supplied by the base ABI.

`client_plugin_config_json` should remain immutable launch configuration. It
should not become a replacement for Draxul's config document or a place to
continually publish camera, scroll, or player state.

### 4. Canvas 2D rendering

ScoreView should not receive an `NVGcontext*` or link against Draxul's C++
`INanoVGPass`.

Add a frame-scoped `draxul.canvas2d.v1` command table covering:

- Paths, lines, and Beziers.
- Fill/stroke colors and gradients.
- Save/restore and transforms.
- Scissors.
- Rectangles and rounded rectangles.
- Font loading, text measurement, and text drawing.

This is almost exactly the NanoVG subset ScoreView currently uses. Draxul can
implement it with NanoVG initially, but the ABI remains backend-neutral. The
host owns clipping, making split-pane isolation much stronger than it is for
arbitrary GPU code.

### 5. ImGui overlay bridge

Both hosts have substantial ImGui interfaces. Reimplementing these as a generic
UI-description language would be a project of its own.

The pragmatic first-party solution is an optional
`draxul.imgui-overlay.v1` extension:

- The plugin owns its ImGui context and UI logic.
- Draxul begins the renderer backend frame and submits the draw data.
- The SDK carries an exact compatibility fingerprint covering ImGui version,
  compile options, and draw structure sizes.
- A mismatch produces the normal inert plugin placeholder.

This extension should be explicitly considered build-matched and first-party.
Portable third-party plugins should use Canvas 2D or native GPU rendering
instead.

### 6. Device leases

Initially, ScoreView should keep its existing SDL audio, microphone, and RtMidi
implementation inside the plugin. These boundaries are already reasonably
isolated.

Add a small per-process lease service for resources such as:

- `audio-output`
- `microphone`
- `midi:<device>`

This prevents two ScoreView panes in one UI from unexpectedly fighting over
devices. If several future plugins need audio, a full host-owned audio API can
be introduced later rather than prematurely designing one now.

## SatView migration

SatView should go first. Its host already separates core, scene, services, and
renderer modules.

### 1. Extract `SatViewRuntime`

Move lifecycle, camera/input, simulation, service pumping, and UI state out of
`SatViewHost`. Keep a thin static adapter temporarily so the old and plugin
versions can run from the same runtime.

### 2. Refactor the renderer boundary

`SatViewScenePass` currently derives from Draxul's C++ `IRenderPass` and casts
to Draxul-specific Vulkan and Metal contexts. It needs backend adapters accepting
the public plugin frame structures instead.

The major Vulkan issue is resource upload. The current renderer uses Draxul's
VMA allocator, performs queue submissions and waits, and sometimes calls
`vkDeviceWaitIdle`. Those operations are incompatible with the plugin contract.

Refactor it to:

- Own its Vulkan memory directly.
- Record staging copies onto the borrowed command buffer.
- Retire staging and replaced textures by buffered frame slot.
- Never submit, queue-wait, or device-wait during rendering.
- Use renderer idle only during final destruction or restart.

### 3. Move resources under the plugin directory

SatView currently resolves many assets relative to the executable or source
tree. Package catalogs, textures, and shaders inside the plugin directory and
inject an asset root into the SatView loaders.

### 4. Separate state

- Shared topology config: initial view and launch options only.
- Local storage: filters, UI state, and camera preferences.
- Module-shared state: catalog downloads and cache can be shared between
  SatView instances in one process.
- Per-pane state: camera, time controls, selection, and simulation
  presentation.

### 5. Package and switch

Build `draxul-satview.dll` or the macOS module, manifest, shaders, catalogs, and
textures. Keep static SatView behind a transition flag until render, input, and
configuration parity is established.

## ScoreView migration

ScoreView is more involved because `ScoreHost` currently owns document loading,
Verovio, asynchronous engraving, presentation, transport, player learning,
audio, microphone/MIDI, and ImGui.

### 1. Extract `ScoreRuntime`

Pull the state machine currently in `ScoreHost::pump()` into a renderer-independent
runtime. Preserve the existing controllers and their offline-testable seams.

### 2. Replace the NanoVG dependency

Adapt `ScorePresentation`, `score_render_nvg`, keyboard rendering, and analysis
overlays to a small internal `IScoreCanvas`. Implement:

- `NanoVGScoreCanvas` for the current static host.
- `PluginScoreCanvas` over `draxul.canvas2d.v1`.

This permits dual-running the same presentation logic during migration.

### 3. Preserve private workers and devices

Keep Verovio engravers, audio synthesis, microphone analysis, and MIDI inside
the module. The plugin tick drains their results on the main thread. Device
callbacks must be stopped during `quiesce_instance`.

### 4. Relocate resources

ScoreView currently looks beside the executable for `verovio-data`, the Leipzig
font, and soundfonts. Package these under the ScoreView plugin and resolve them
relative to `plugin_directory`.

### 5. Define source configuration

Do not return to `source_path`. Use plugin configuration such as:

```json
{
  "source": {
    "kind": "local-file",
    "path": "D:/scores/piece.musicxml"
  },
  "mode": "roll"
}
```

This highlights an unavoidable issue: another attached UI may not possess that
local file. It should preserve the pane and show an actionable local
placeholder. A later `server-blob` or content-addressed resource kind could make
scores portable between attached clients.

### 6. State ownership

- Source identity and initial mode: shared launch configuration.
- Scroll, inspector state, and device choice: local plugin storage.
- Player progress: local durable storage keyed by source content, preserving
  current behavior.
- Live playback should not be shared server state in this migration.

## Main risks

- **ImGui compatibility:** the least portable part; it needs an explicit SDK
  fingerprint.
- **SatView GPU ownership:** its current queue submissions, VMA dependency, and
  device waits must be removed, not exposed through the ABI.
- **Multiple UIs:** every attached UI creates its own plugin instance. ScoreView
  must not automatically start audio or microphone merely because a remote pane
  exists.
- **Hidden panes:** rendering, logical ticking, and audio need separate policies.
- **Local score paths:** shared topology does not imply shared filesystems.
- **Teardown:** workers and device callbacks must quiesce before renderer idle
  and instance destruction.
- **Packaging:** ScoreView's Verovio data, soundfonts, and native dependencies
  make its plugin substantially larger than the triangle.
- **Crash isolation:** both remain trusted in-process native code; either plugin
  can still crash the UI.
- **Split safety:** Canvas 2D can enforce clipping; raw SatView GPU code remains
  trusted and must be followed by host state restoration.

## Implementation vertical slices

Each slice must finish as a usable path through a real dynamically loaded module
or through the existing product host rewired to the runtime seam that the module
will use. A service-table implementation with no pane exercising it is not a
finished slice.

The static SatView and ScoreView hosts remain available only as parity oracles
until their respective cutover slices. They should not accumulate new features
after extraction begins.

### Slice 1: ABI v2 through the spinning triangle

Progress: completed on Windows on 2026-08-12; Metal build/render remains part of
the cross-platform CI gate.

**User-visible result:** the existing triangle still launches from a server
mutation, animates smoothly, handles input, pauses while hidden, and tears down
cleanly, now entirely through the new current-only ABI.

Implement:

- Replace the v1 query symbol and tables with ABI v2; do not retain a v1 loader.
- Add host-service and plugin-extension queries.
- Add independent render and logic deadlines, thread-safe `request_tick`, and
  `quiesce_instance`.
- Add the presentation extension with display name, status, background,
  content-ready/error state, cursor, actions, and print hint.
- Convert the triangle, all fixture modules, manifests, loader diagnostics, and
  SDK documentation in the same change.
- Have the triangle expose observable metadata and actions, for example
  `toggle_pause`, `reverse`, and a status line containing its animation state.
- Preserve hidden-pane behavior: no render deadline while hidden; showing the
  pane requests one fresh frame.

Integration proof:

- Start an isolated server, create a triangle tab and split using `draxul.exe`,
  and verify the topology allocates no terminal.
- Attach the UI and verify animation, Space-to-pause, click-to-reverse, resize,
  hide/show, action dispatch, status, and pane close/restart.
- Load real valid, malformed, wrong-ABI, missing-symbol, wrong-ID, and
  unsupported-backend modules.
- Retain the fixed-angle Vulkan and Metal snapshots and the neighboring-pane
  isolation scenario.

Exit gate: no v1 names or compatibility path remain in production code, and the
triangle proves every new lifecycle callback through `PluginHost` rather than a
direct fixture call.

### Slice 2: Durable local plugin state through the triangle

Progress: completed on Windows on 2026-08-12. The Metal implementation was
updated in lockstep and remains a macOS CI build/render gate.

**User-visible result:** a plugin can keep client-local preferences across a UI
restart without changing shared server topology.

Implement:

- Add versioned path and storage services for plugin resource, configuration,
  data, cache, and temporary directories.
- Add atomic bounded JSON read/write scoped by plugin ID, with an optional
  per-pane key beneath that scope.
- Make storage callable only under documented thread rules; worker callers use
  `request_tick` to marshal installation onto the main thread.
- Add `remember_state` to the triangle configuration. When enabled, pause state
  and direction survive a UI restart and are reflected in status text.
- Ensure launch configuration remains immutable and server-shared while saved
  state remains local to each attached UI.
- Make corrupt or inaccessible state fall back to defaults with an actionable
  warning rather than destroying the shared pane.

Integration proof:

- Create one pane from the CLI, change its state in the UI, restart only the UI,
  and observe restored local state against the same server pane.
- Attach a second UI with an empty data directory and prove it resolves the same
  topology independently.
- Verify atomic replacement, size bounds, corrupt-state recovery, and removal of
  an instance while a write is pending.

Exit gate: the triangle is the real consumer; no generic storage service is
merged solely on unit tests.

### Slice 3: SatView runtime extraction behind the existing host

Progress: completed on Windows on 2026-08-12. The prior host implementation is
now `SatViewRuntime`, does not inherit `IHost`, submits through a SatView-owned
frame sink, uses a SatView-owned callback port, and accepts injected asset/cache
roots. `SatViewHost` is a stateless parity adapter. macOS remains a CI gate.

**User-visible result:** the statically launched SatView behaves and renders as
before, but all product behavior is owned by a renderer-independent
`SatViewRuntime` that a plugin can instantiate.

Implement:

- Extract lifecycle, camera/input, catalog/cloud services, simulation worker,
  local settings, actions, status, and presentation state from `SatViewHost`.
- Define internal product ports for frame rendering, overlay submission,
  persistence/paths, logging, wake/tick requests, and chrome metadata.
- Keep these ports in the SatView module; do not expose Draxul `IHost`, renderer,
  SDL, ImGui, or STL objects through the native plugin ABI.
- Rewire the existing `SatViewHost` as a thin adapter over `SatViewRuntime`.
- Inject asset and cache roots rather than resolving them from the executable or
  repository inside product logic.
- Preserve deterministic offline catalog/cloud hooks and current simulation
  worker tests.

Integration proof:

- Run the existing static SatView through app startup, input, UI controls,
  background catalog delivery, hidden visibility, shutdown, and render
  snapshots.
- Add a runtime orchestration test that uses production adapters or narrow fakes
  at external boundaries, rather than inspecting newly extracted helpers.
- Compare status/config output and deterministic images before and after the
  extraction.

Exit gate: `SatViewHost` contains adaptation, not product state or simulation
policy, and the existing launch path is still fully functional.

### Slice 4: SatView scene as a real GPU plugin

Progress: completed on Windows on 2026-08-12. `dev.draxul.satview` is a real
DLL with a matching Metal module source, staged assets/shaders, live
`SatViewRuntime` catalog and simulation work, pane-local input, deterministic
render coverage, and no imported queue submit/wait or device-wait calls. The
plugin-native overview renderer deliberately replaces the old tightly coupled
HDR pass rather than retaining that pass as a compatibility fallback. Metal
build/render validation remains assigned to CI.

**User-visible result:** `dev.draxul.satview` can be created as a tab or split and
renders the live SatView scene, including camera input and background simulation,
without being statically registered as the active pane host. The optional ImGui
control panel may remain disabled in this slice.

Implement:

- Build Windows and macOS module targets that instantiate `SatViewRuntime`.
- Adapt `SatViewScenePass` to the ABI v2 Vulkan and Metal frame contracts.
- Remove its dependency on Draxul's C++ render contexts.
- Replace VMA/queue-submit/queue-wait/device-wait upload paths with plugin-owned
  memory, borrowed-command-buffer copies, and frame-slot retirement.
- Stage shaders, textures, star/constellation/surface catalogs, and other assets
  under the plugin directory.
- Drive catalog/cloud and simulation completion through logic ticks; coalesce
  updates while hidden and request a fresh render when shown.
- Supply basic presentation metadata and a missing-asset placeholder.

Integration proof:

- Create SatView via `pane split --plugin dev.draxul.satview` and `tab create
  --plugin dev.draxul.satview` against an isolated server.
- Put it beside a terminal, manipulate the camera, resize the split, switch tabs
  and Spaces, restart the UI, and close the pane while workers are active.
- Compare a fixed-time/fixed-camera plugin image with the static SatView oracle
  on Vulkan and Metal.
- Verify validation layers report no ownership/layout errors and no upload path
  submits or waits on a queue supplied by Draxul.

Exit gate: the dynamic module is useful without debug UI, observes hidden-pane
scheduling, and cannot alter the neighboring pane in the snapshot test.

### Slice 5: SatView controls and complete cutover

Progress: completed on Windows on 2026-08-12. ABI v2 now includes a
build-fingerprinted first-party ImGui overlay service; the dynamic module runs
the existing SatView dock/control panels, translates pane-local input, exposes
product actions/status, and persists its preferences through pane-local plugin
storage. Production startup no longer registers or links the static SatView
host, legacy executable shader/asset staging was removed, and a missing module
therefore remains an inert plugin pane rather than selecting compiled-in code.

**User-visible result:** the dynamic SatView has its complete control panels,
saved preferences, status/actions, and launch behavior; the statically linked
SatView host is removed from the executable.

Implement:

- Add the build-matched first-party ImGui overlay service, compatibility
  fingerprint, input capture, cursor, font rebuild, and IME/text-area support.
- Connect the existing SatView ImGui controls through that service without
  moving product UI into `app/`.
- Persist filters, camera/UI preferences, and service settings through plugin
  storage; keep launch-only values in shared plugin configuration.
- Route all SatView actions and chrome metadata through plugin extensions.
- Change palette/config launch targets to `dev.draxul.satview`.
- Remove SatView provider registration and the executable's static
  `draxul-satview-host` link. Keep reusable SatView module libraries private to
  the plugin target.

Integration proof:

- Exercise every major control-panel group, keyboard/mouse capture, font/scale
  change, persistence, hide/show, and restart through a dynamically loaded pane.
- Run static-versus-plugin image comparisons immediately before deleting the
  static adapter, then keep only the plugin references.
- Build an app configuration in which SatView is present only as its staged
  module and verify `draxul plugin get dev.draxul.satview --json`.

Exit gate: removing or renaming the SatView DLL/module produces an inert pane
with an actionable error, not a fallback to compiled-in SatView code.

### Slice 6: ScoreView runtime and canvas seam behind the existing host

Progress: completed on Windows on 2026-08-12. The prior ScoreHost orchestration
is now `ScoreRuntime`: it owns the product state and workers without inheriting
`IHost` or accepting `IFrameContext`. Rendering exits through a product-owned
`ScoreFrameSink`; the small retained static adapter supplies NanoVG and ImGui
frame sinks for parity tests while the dynamic canvas route is introduced.

**User-visible result:** the existing ScoreView still supports paged and flow
modes, scrolling, zooming, engraving, playback, and its inspector, but product
logic no longer owns a Draxul NanoVG pass or `IHost` implementation details.

Implement:

- Extract `ScoreRuntime` from `ScoreHost`, including source loading, layout,
  transport, stream/session controllers, player rig, audio coordination,
  actions, metadata, and print state.
- Introduce product-internal `IScoreCanvas`, device, storage/path, clock, and
  wake/tick ports.
- Convert `ScorePresentation`, score rendering, keyboard rendering, and analysis
  overlays from direct NanoVG calls to `IScoreCanvas`.
- Implement `NanoVGScoreCanvas` for the temporary static adapter.
- Inject Verovio data, Leipzig font, soundfont, and progress roots.
- Keep asynchronous engraver behavior and the existing no-concurrent-Verovio
  invariant intact.

Integration proof:

- Run the existing static ScoreView with a deterministic score through paged,
  flow, rolling-window, inspector, print crop, restart, and worker shutdown.
- Compare rendered output and status/player state before and after extraction.
- Retain fake microphone, MIDI, clock, and audio seams for deterministic tests.

Exit gate: the static `ScoreHost` is an adapter; presentation and runtime code no
longer mention `NVGcontext`, Draxul frame contexts, executable-relative paths, or
global config documents.

### Slice 7: ScoreView reading modes as a real plugin

Progress: completed on Windows on 2026-08-12. `dev.draxul.scoreview` is now a
real ABI-v2 module with packaged Verovio/Leipzig/soundfont resources. The host
records the module's build-matched Canvas 2D pass in the same clipped pane
viewport as its native plugin pass, then composites its ImGui inspector. A real
Swan Lake MusicXML fixture renders through the DLL, and isolated-server CLI
coverage proves ScoreView tabs retain a terminal-free plugin descriptor. Metal
build and image validation remain part of CI.

**User-visible result:** `dev.draxul.scoreview` opens a local MusicXML score in a
dynamic pane and supports paged/flow presentation, resize, scroll, zoom, actions,
printing, status, and the inspector. Live playback and physical input can remain
disabled in this slice.

Implement:

- Add the host-owned `draxul.canvas2d.v1` service with frame-local handles and
  enforced pane clipping.
- Implement `PluginScoreCanvas` over that service.
- Build and stage the ScoreView DLL/macOS module, manifest, Verovio runtime data,
  Leipzig font, default soundfont metadata, and any native dependencies/rpaths.
- Accept structured `source` and `mode` plugin configuration.
- Use the existing ImGui overlay service for the inspector.
- Return presentation metadata and print hints through plugin extensions.
- Show an actionable placeholder when a local source is absent on this UI.

Integration proof:

- Create a ScoreView tab and split from the CLI using a local MusicXML source.
- Verify no terminal is allocated and that layout rollback remains atomic.
- Exercise page/flow mode, zoom, scrolling, resize, inspector controls, print
  crop, restart, and a split beside a terminal.
- Run deterministic Vulkan and Metal image comparisons and prove Canvas 2D
  cannot draw outside the pane scissor.
- Attach a second UI without the source file and verify only that UI shows the
  placeholder while shared topology remains intact.

Exit gate: the ScoreView plugin is a useful score reader and uses no raw Vulkan
or Metal entry point for its normal 2D presentation.

### Slice 8: ScoreView transport, workers, and player persistence

Progress: completed on Windows on 2026-08-12. Transport, rolling-window
engraving, adaptive player state, and content-keyed progress now execute inside
the dynamic module. Hidden panes pause and remember an active transport by
default, resume it when shown, suppress redraw callbacks, and poll only an
in-flight worker at 100 ms. `background_playback: true` retains the 16.7 ms
logic cadence without scheduling hidden render deadlines. Quiesce ends the
session, flushes progress, and joins engraving before renderer-safe teardown.

**User-visible result:** the dynamic ScoreView runs its flow/roll transport,
asynchronous rolling engraver, adaptive player model, and durable progress with
smooth animation and correct hidden-tab behavior.

Implement:

- Drive transport and engraver completion through plugin logic deadlines and
  `request_tick` rather than render polling.
- Separate render visibility from playback policy. Default to pausing transport
  when the pane becomes hidden unless launch configuration explicitly opts into
  background playback.
- Persist player progress by source content through plugin storage.
- Keep scroll/inspector/device preferences client-local and source/mode launch
  information server-shared.
- Quiesce the engraver and flush progress before instance destruction.
- Surface progress, tempo, score, worker state, and errors through status
  metadata without forcing frames solely to update chrome.

Integration proof:

- Run a deterministic fake-clock ScoreView session through roll, judgment,
  background engraving, window swap, hide/show, UI restart, and progress reload.
- Verify hidden panes have no render deadlines and follow the selected transport
  policy.
- Close/restart repeatedly while engraving is active and run the existing worker
  stress scenarios through the real plugin boundary.

Exit gate: the dynamic plugin matches the current non-device ScoreView product
behavior and has no main-loop polling dependency.

### Slice 9: ScoreView audio, microphone, and MIDI

**User-visible result:** the dynamic ScoreView supports soundfont output,
metronome/audition, microphone input, and MIDI input with deterministic ownership
when multiple panes or attached UIs exist.

Implement:

- Add process-local leases for audio output, microphone, and named MIDI devices.
- Acquire devices only after an explicit local interaction or configuration;
  merely resolving a shared server pane must not open them.
- Keep SDL audio/microphone and RtMidi private to the ScoreView module for this
  version, including macOS permission preflight.
- Define focused/hidden/background behavior for each device and display lease
  conflicts as actionable local status/errors.
- Stop device callbacks and release leases during `quiesce_instance` before
  renderer idle or module teardown.

Integration proof:

- Use fake device providers in automated plugin-host integration tests for
  acquisition, contention, callback delivery, hide/show, restart, and teardown.
- Smoke real output, MIDI, and microphone manually on Windows and macOS.
- Open two ScoreView panes and two attached UIs and verify that passive instances
  do not steal active devices.

Exit gate: no device callback can enter destroyed plugin state, and a missing or
busy device degrades to a usable visual ScoreView rather than failing the pane.

### Slice 10: ScoreView cutover and architecture cleanup

**User-visible result:** SatView and ScoreView are both discovered, packaged,
launched, persisted, and controlled as plugins; Draxul contains no static product
fallback for either one.

Implement:

- Change ScoreView palette/config launch targets to
  `dev.draxul.scoreview`.
- Remove ScoreView provider registration and the executable's static host link.
- Delete temporary static adapters, transition flags, executable-relative asset
  paths, and obsolete host-only tests.
- Keep lower product libraries under `modules/`, linked privately into their
  modules; keep generic plugin services under `libs/`.
- Update CLI help, `docs/features.md`, `docs/module-map.md`, manifests, packaging,
  release checks, and the Draxul agent skill.
- Verify clean installations and missing/replaced module behavior.

Integration proof:

- From a clean build/install, use only `draxul.exe` server mutations to create a
  Space containing terminal, SatView, and ScoreView splits; attach, detach, and
  reattach the UI and verify convergence.
- Restart the server and UI, verify topology plus local state restoration, then
  remove each module in turn and verify inert placeholders.
- Run the complete Windows and macOS build, smoke, integration, and render
  suites with no static SatView/ScoreView symbols linked into the executable.

Exit gate: the plugin route is the sole production route for both products, and
deleting either plugin package does not prevent Draxul or unrelated panes from
starting.

### Dependency shape

```text
Slice 1 (ABI/lifecycle triangle)
  -> Slice 2 (storage triangle)
      -> Slice 3 -> Slice 4 -> Slice 5 (SatView cutover)
      -> Slice 6 -> Slice 7 -> Slice 8 -> Slice 9 -> Slice 10
                                  (ScoreView cutover and cleanup)
```

SatView should be completed through Slice 5 before ScoreView Slice 7 is merged,
because SatView is the proving ground for the ImGui bridge, resource packaging,
GPU teardown, and current-only module release process. ScoreView runtime
extraction in Slice 6 can proceed independently once the common ABI and storage
contracts from Slices 1 and 2 are stable.

### Definition of done for every slice

- The capability is exercised through a real `PluginHost` or a complete existing
  host rewired to the exact runtime seam the plugin will consume.
- An isolated-server CLI integration proves topology creation and terminal-free
  allocation where applicable.
- The attached UI supplies the smoke path; a test that calls only the plugin API
  directly is supporting coverage, not acceptance.
- Windows/Vulkan and macOS/Metal code paths are implemented together. If only one
  platform can be run locally, the other must build and run in CI before merge.
- Build Draxul and its integration targets, run `py do.py smoke`, run the focused
  CTest labels, and run relevant deterministic render scenarios.
- New user-visible commands, behavior, packaging, and architecture are reflected
  in CLI help, `docs/features.md`, `docs/module-map.md`, and the Draxul agent skill
  in the slice that introduces them.
- A slice must leave the branch releasable. Temporary static adapters are allowed
  only where named above and are removed at their explicit cutover gate.

## Validation strategy

Testing should be dominated by vertical slices:

- Real DLL/module loading.
- CLI-created plugin panes and tabs.
- Attached UI convergence.
- Deterministic Vulkan and Metal images.
- Split-pane clipping and isolation.
- Hidden-tab scheduling behavior.
- Restart and persistence.
- Fake device-backed ScoreView sessions.
- Windows and macOS smoke coverage.

Unit tests are most useful around service-table negotiation, configuration
migration, and isolated runtime algorithms. They should not replace end-to-end
plugin loading and rendering coverage.
