# SatView Threaded Refresh Plan

## Goal

Make SatView animation smooth by moving satellite position updates off the app/render loop, publishing render-ready satellite data through a non-blocking snapshot handoff, and making `--continuous-refresh` drive rendering at the renderer/display cadence instead of the current 33 ms host tick.

The render path must never wait on the satellite update thread. It should read the latest complete snapshot and draw it, even if the worker is still computing the next one.

## Current Behavior

- `SatViewHost::next_deadline()` returns `now + 33 ms`, so SatView animates at roughly 30 Hz.
- `SatViewHost::pump()` advances `simulated_seconds_`, conditionally propagates satellites, then calls `request_redraw()`.
- SGP4 propagation is throttled by `kPropagationTick = 1s` and by a `60s` simulated-time delta threshold, so satellite positions update in visible steps.
- `draw()` rebuilds track/marker line vertices from `propagation_snapshot_` every frame and `SatViewScenePass::set_scene_vertices()` copies them into an owned vector and increments the scene revision.
- `--continuous-refresh` is passed into host launch options, but SatView currently ignores it.

## Target Architecture

## Implementation Status

This slice implements the continuous-refresh behavior, the non-blocking triple-buffer snapshot handoff, the background SGP4 simulation worker, and render-side interpolation between worker-published marker samples.

The render pass data path now uses separate track and marker streams: orbit tracks upload as line-list vertices only when track data or visual filters change, while markers upload as compact per-satellite instances and are expanded into camera-facing crosses in the shader. A follow-up can still move more filtering/packing into the worker, but camera-only redraws no longer force full marker vertex rebuilds.

### 1. SatView Simulation Worker

Add a module-local worker, tentatively `SatViewSimulationWorker`, owned by `SatViewHost`.

The worker owns CPU-only satellite simulation state:

- current catalog/propagation model
- simulation clock state: base simulation time, base steady time, speed, paused state
- LOD settings: track count, track samples, marker cap
- selected NORAD id
- filter state if we choose to publish already-filtered buffers

The worker loop:

1. Apply pending command/config updates from the host.
2. Rebuild the SGP4 model when the catalog generation changes.
3. Compute current simulation time from steady time and speed.
4. Propagate marker positions on a fixed simulation cadence.
5. Rebuild orbit track vertices only when catalog/LOD/selection settings change, or at a slower track cadence.
6. Publish a complete immutable render snapshot.
7. Sleep until the next simulation tick or until a host command wakes it.

Only CPU data is touched in this thread. No ImGui, renderer, Vulkan, Metal, SDL, or host callback calls from the worker.

### 2. Non-Blocking Snapshot Handoff

Use an RCU-style triple buffer instead of a mutex in the draw path.

Proposed shape:

```cpp
struct SatViewRenderSnapshot
{
    uint64_t generation = 0;
    double simulation_seconds = 0.0;
    std::chrono::steady_clock::time_point produced_at{};
    std::string source_label;
    std::string status_text;
    std::vector<SatViewSceneVertex> track_vertices;
    std::vector<SatViewMarkerState> markers;
    std::vector<SatellitePropagatedState> states_for_picking;
};

struct SnapshotSlot
{
    std::atomic<uint32_t> readers{0};
    SatViewRenderSnapshot snapshot;
};
```

Publication:

- producer writes only to a slot that is not currently published and has `readers == 0`
- producer publishes by storing the slot index with `memory_order_release`
- if no slot is free, skip this publish rather than blocking

Reading:

- render thread loads the published slot with `memory_order_acquire`
- increments that slot's reader count
- rechecks the published slot index; if it changed, releases and retries
- reads spans/references from that stable snapshot
- decrements reader count when the draw-side guard is destroyed

This keeps the renderer read path lock-free and bounded. The render thread either sees the newest full snapshot or the previous full snapshot; it never sees partially-written satellite data.

### 3. Render Data Split

Split the render data into slower and faster streams:

- Earth uniforms: computed every draw from the current render simulation time.
- Track vertices: updated rarely, because orbit path geometry is comparatively expensive and does not need to be rebuilt at display rate.
- Marker positions: updated at the worker simulation cadence.

The current CPU-built camera-facing marker crosses should be replaced with a marker buffer plus shader expansion:

- worker publishes marker centers, color, size, selected flag, and NORAD id
- shader uses camera right/up from uniforms to expand each marker into a small cross or quad
- this avoids rebuilding marker vertices on the main thread just because the camera moved

### 4. Smooth Marker Motion

The first worker version can publish marker positions at 60 Hz. That removes the current 1-second SGP4 step.

For smoother motion across different display refresh rates, use interpolation:

- worker publishes two marker positions per satellite: `pos0` at `sim_t0`, `pos1` at `sim_t1`
- render computes `alpha = (render_sim_time - sim_t0) / (sim_t1 - sim_t0)`
- marker shader or CPU upload path interpolates between the two positions

This lets the renderer present at 60/120/144 Hz while the worker publishes at a lower stable cadence. It also prevents jitter when worker ticks and render frames do not line up exactly.

### 5. Continuous Refresh

SatView should store:

```cpp
bool continuous_refresh_enabled_ = false;
```

set from:

```cpp
context.launch_options.request_continuous_refresh
```

Behavior:

- when continuous refresh is enabled, SatView should request a new frame every pump and return an immediate/near-immediate deadline instead of `kFrameTick`
- when disabled, keep the current lower-power deadline path

The app currently makes `--continuous-refresh` also disable vblank. That is not ideal for display-rate animation. The cleaner model is:

- `--continuous-refresh`: keep requesting frames continuously
- `--no-vblank`: disable present/vblank throttling

If this is changed, continuous SatView rendering should naturally present at the display/vsync cadence unless the user explicitly passes `--no-vblank`.

### 6. Host/Main Thread Responsibilities

`SatViewHost` remains responsible for:

- camera input
- ImGui panel
- translating UI changes into worker commands
- acquiring the latest snapshot during draw
- selection hit testing from the latest snapshot
- setting frame uniforms
- recording the render pass

It should stop doing:

- SGP4 propagation in `pump()`
- full scene vertex rebuilding in `draw()`
- direct mutation of propagation snapshots used by rendering

### 7. Catalog Refresh

Keep the existing catalog service initially, then fold it into the worker once the snapshot path is stable.

Incremental route:

1. Keep `SatViewCatalogService` owned by the host.
2. Host detects catalog generation changes and sends an immutable catalog copy to the worker.
3. Worker builds the propagation model and publishes snapshots.

Later route:

1. Worker owns/uses the catalog service.
2. Network/cache refresh is handled entirely off the main thread.
3. Worker publishes catalog status with the render snapshot.

### 8. Implementation Phases

1. **Continuous refresh fix**
   - Make SatView honor `request_continuous_refresh`.
   - Prefer preserving vblank unless `--no-vblank` is set.
   - Keep the existing 33 ms behavior when continuous refresh is off.

2. **Snapshot buffer**
   - Add triple-buffered `SatViewSnapshotExchange`.
   - Add tests for coherent publication, reader stability, and no blocking when a reader holds a slot.

3. **Simulation worker**
   - Move simulation clock and SGP4 propagation out of `SatViewHost::pump()`.
   - Publish marker/state snapshots at a stable cadence.
   - Leave track vertex generation in the worker but on a slower/dirty-only cadence.

4. **Render pass data path**
   - Stop copying scene vertices in `SatViewScenePass::set_scene_vertices()` every draw.
   - Upload marker and track buffers by snapshot generation.
   - Split marker and track GPU buffers so track geometry does not upload every marker tick.

5. **Interpolation**
   - Publish bracketing marker positions and times.
   - Interpolate in shader or in a tiny render-side staging step.
   - Validate at normal speed and high `time_speed_` values.

6. **Catalog refresh ownership**
   - Move catalog refresh/polling into the worker if the host still spends measurable time on catalog handoff.

7. **Validation**
   - Unit-test snapshot exchange and deterministic worker timing with a fake clock.
   - Run SatView render smoke with continuous refresh.
   - Profile CPU time in `pump()`, `draw()`, worker propagation, and GPU upload.
   - Visually verify satellites animate without 1-second stepping.

## Risks

- `std::atomic<std::shared_ptr<T>>` would be simpler but is not guaranteed lock-free. Prefer explicit triple buffering if strict non-blocking render reads matter.
- GPU upload can still become the bottleneck if future features rebuild all marker instances too often. Keep track and marker buffers separated so track geometry does not upload at marker cadence, and keep marker expansion in the shader so camera-only redraws stay cheap.
- Worker threads must not call `IHostCallbacks::request_frame()` unless that callback is made explicitly thread-safe. Use host deadlines or a dedicated thread-safe wake path.
- Selection and filtering currently depend on the host-side propagation snapshot. Those need to move to snapshot reads or worker-published filtered data.
