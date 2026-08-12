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

## ABI additions

Keep the existing ABI v1 compatible by appending size-checked fields to the
existing tables in `libs/draxul-plugin/include/draxul/plugin_api.h`. Add two
generic extension mechanisms:

```c
// Plugin asks Draxul for an optional host service.
query_host_service(host_context, service_id, minimum_version, out_table, out_size);

// Draxul asks the plugin for an optional capability.
query_plugin_extension(instance, extension_id, minimum_version);
```

Every returned service has its own version and `struct_size`. This avoids
creating one enormous ABI v2 and allows old triangle plugins to continue
working.

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

## Recommended delivery order

1. Add extension/service negotiation, logic ticks, quiescing, and presentation
   metadata.
2. Add storage/path services and integration fixtures.
3. Extract and ship SatView as a plugin alongside the static host.
4. Establish SatView Vulkan/Metal snapshot, resize, input, hidden-tab, and
   restart parity.
5. Add Canvas 2D and the build-matched ImGui bridge.
6. Extract and ship ScoreView alongside its static host.
7. Verify page/flow rendering, engraving workers, persistence, printing, audio,
   microphone, and MIDI.
8. Make plugin launches the default.
9. Remove static registration from `app/main.cpp` and eventually remove the
   SatView/ScoreView executable link flags.

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
