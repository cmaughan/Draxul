# SatView Plan

## Goal

Build a new optional module, `satview`, that gives Draxul a 3D overview of Earth and the objects orbiting it. The view should show:

- A rotatable, zoomable planet Earth.
- A visible day/night cycle.
- Satellite orbit tracks above the globe.
- Eventually, live or recently updated satellite positions and metadata.

The module should follow the existing module pattern used by `modules/megacity`: keep the terminal app free of source-level dependency on the module, gate the build behind a CMake option, and self-register the host provider from `app/main.cpp` only when enabled.

## Product Plan

### First Slice

The first usable slice is a visual host named `satview`:

- Launch via `--host satview`.
- Render inside the existing `IFrameContext::record_render_pass()` path.
- Use the shared 3D renderer abstractions instead of creating a second window or standalone renderer.
- Provide mouse drag rotation and mouse wheel zoom.
- Render a real Earth sphere, not a screen-facing billboard.
- Render synthetic orbit rings as a preview of the final satellite visualization.
- Keep Vulkan and Metal implementations in sync.

This slice is intentionally not a full orbital-mechanics implementation. It proves the host lifecycle, module wiring, shader staging, renderer depth path, and the basic visual language before introducing live data and larger CPU/GPU data flows.

### Full Feature Direction

The final version should turn the preview rings into a real catalog-driven orbital view:

- Fetch current orbital element sets for active Earth-orbiting objects.
- Cache the downloaded catalog and avoid frequent external requests.
- Propagate satellite positions locally so camera movement and time controls are instant.
- Draw orbit paths and current satellite markers.
- Filter by orbit class, owner/operator, object type, altitude, and search text.
- Show selected-object details such as name, catalog id, international designator, apogee/perigee, inclination, period, and element age.
- Support offline use from the last good cache.

## Data Source Plan

### Preferred Public Source: CelesTrak GP Data

CelesTrak exposes current General Perturbations data and supports query URLs such as:

```text
https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=json
https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=csv
https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=tle
```

Use JSON or CSV for the production path rather than legacy fixed-width TLE parsing where possible. CelesTrak explicitly notes that newer GP formats support 9-digit catalog numbers and avoid TLE-era fixed-field limitations. Also respect their update cadence and cache policy: active and Starlink group downloads should not be repeated more than once per update, and data is updated roughly every 2 hours.

Useful groups for staged rollout:

- `active` for the main catalog.
- `stations` for a tiny test set.
- `visual` or `brightest` for a user-friendly demo subset.
- `starlink`, `oneweb`, `gps-ops`, and similar groups for filtered views.

### Optional Authenticated Source: Space-Track

Space-Track is useful as a future authenticated source for more complete catalog data and historical queries. The implementation should treat it as optional because it requires credentials and terms-aware access.

Space-Track documentation points developers toward the `gp` and `gp_history` API classes for newer General Perturbations data. The older API class names tied to formats, such as `tle` and `tle_latest`, should not be the long-term foundation.

### Cache And Update Policy

- Never fetch satellite data on the render thread.
- Add a small data service owned by the SatView host or a module-local controller.
- Store the last successful catalog under the existing app cache/config conventions once those are identified.
- Use an age-based refresh policy, defaulting to no more than once every 2 hours for CelesTrak `active`.
- On network failure, keep rendering from the last cache and surface a non-blocking status string.
- Ship or generate a tiny built-in sample for smoke tests so rendering does not require network access.

## Orbit Math Plan

### Representation

Internally store each object as:

- Stable id: NORAD catalog id.
- Display fields: name, object type, country/operator if available, international designator.
- Element epoch and source age.
- Mean elements from GP/OMM/TLE.
- Derived orbit class: LEO, MEO, GEO, HEO, debris/other.
- Last propagated ECI/ECEF position.
- Cached orbit polyline samples for the current time window.

### Propagation

Use a real SGP4 implementation rather than a hand-rolled approximation. The implementation options should be evaluated in this order:

1. Small permissively licensed C/C++ SGP4 implementation that can be vendored or fetched through CMake.
2. Existing space-data standards code if the dependency shape is reasonable.
3. A module-local minimal wrapper around a proven SGP4 source, with tests against known reference vectors.

The render path should receive already-propagated positions in Earth-centered coordinates. CPU propagation should run at a lower cadence than rendering, with interpolation if needed.

### Coordinate Frames

Start with a simple Earth-centered inertial approximation for orbit paths, then add Earth-fixed conversion:

1. Parse GP/OMM/TLE element set.
2. Propagate with SGP4 to TEME/ECI position.
3. Convert to Earth-fixed coordinates using UTC time and sidereal rotation.
4. Render positions against an Earth sphere whose texture surface uses the same Earth rotation model.

The first live-data milestone can tolerate a simple GMST conversion, but the implementation should isolate this math so better Earth orientation handling can be added later.

## Rendering Plan

### Module Shape

```text
modules/satview/
  CMakeLists.txt
  draxul-satview/
    CMakeLists.txt
    include/draxul/satview/satview_host.h
    src/satview_host.cpp
    src/satview_scene_pass.h
    src/satview_render_vk.cpp
    src/satview_render.mm
```

Host responsibilities:

- Own camera state, time controls, data-service state, and the scene pass.
- Convert viewport dimensions into view/projection matrices.
- Register the render pass each frame.
- Keep input handling platform-neutral.

Render pass responsibilities:

- Own backend-specific GPU pipeline state.
- Draw Earth.
- Draw orbit tracks and satellite markers.
- Respect `requires_main_depth_attachment()`.
- Keep Vulkan and Metal shader semantics aligned.

### Current Rendering Decisions

- Earth is generated as sphere geometry in the render pass and shaded by the Earth fragment shader.
- Earth shading now uses staged 8k equirectangular day, night, and cloud maps from Solar System Scope, with approximate sun lighting, ocean specular, cloud blend, and atmospheric rim.
- Orbit rings are procedural line lists in the shader for the preview.
- Vulkan uses SPIR-V shaders staged by the existing shader compile path.
- Metal uses `satview_scene.metal` compiled into a metallib by the existing Metal shader staging path.
- Earth texture assets are staged from `assets/satview/textures/` next to the executable and fall back to simple solid colors if missing.
- The renderer depth path must load existing color while clearing depth when a 3D custom pass runs after chrome/grid drawing.

### Scaling Direction

Synthetic rings are cheap, but real satellite rendering can involve many thousands of objects. The scalable target is:

- One CPU-side catalog vector.
- One GPU buffer for current satellite marker positions.
- One GPU buffer for orbit path vertices or a generated path texture/SSBO.
- Batched draws by orbit class or visual style.
- Optional level-of-detail controls to reduce orbit path density when zoomed out.

## Implementation Plan

### Phase 1: MVP Visual Host

- [x] Add `DRAXUL_ENABLE_SATVIEW` CMake option.
- [x] Add `modules/satview` and `draxul-satview` targets.
- [x] Add `HostKind::SatView`, CLI parsing, command palette entry, and app registration.
- [x] Add `SatViewHost` with rotate, zoom, time-speed, pause, status text, and viewport handling.
- [x] Add Vulkan render pass and SPIR-V shaders.
- [x] Add Metal render pass and metallib shader.
- [x] Draw texture-mapped Earth with day/night shading.
- [x] Draw preview orbit rings.
- [x] Fix renderer depth attachment lifetime for 3D custom passes after existing color work.
- [x] Stage and load real Earth day, night, and cloud texture assets.
- [x] Update `docs/features.md`.
- [x] Validate with release build, SatView smoke, and `python do.py smoke`.

### Phase 2: Catalog Fetch And Cache

- [ ] Add module-local satellite catalog types.
- [ ] Add parser for CelesTrak GP JSON or CSV.
- [ ] Add a tiny sample catalog fixture for tests and smoke.
- [ ] Add async or deadline-friendly fetch path that never blocks rendering.
- [ ] Add cache read/write with source URL, fetch time, element epoch range, and object count.
- [ ] Add rate-limit guard so the app does not re-download large groups too often.
- [ ] Add status text for loading, cache age, network failure, and object count.

### Phase 3: SGP4 Propagation

- [ ] Choose and integrate a proven SGP4 implementation.
- [ ] Add unit tests against published/reference propagation cases.
- [ ] Convert catalog records into propagator inputs.
- [ ] Propagate current satellite positions for the selected simulation time.
- [ ] Generate orbit track samples for a configurable time horizon or one orbital period.
- [ ] Separate propagation cadence from render cadence.

### Phase 4: Real Satellite Rendering

- [ ] Replace synthetic rings with catalog-derived orbit tracks.
- [ ] Add satellite point markers.
- [ ] Color by orbit class and object type.
- [ ] Add selection/highlight rendering.
- [ ] Batch marker and track draws to avoid one draw per satellite.
- [ ] Add LOD controls for path segment count and marker density.
- [ ] Keep Vulkan and Metal behavior visually equivalent.

### Phase 5: Interaction And Filtering

- [ ] Add keyboard shortcuts or command actions for pause, time speed, reset camera, and data refresh.
- [ ] Add hover or click selection for nearest satellite marker.
- [ ] Add search/filter model independent of ImGui so it can be tested.
- [ ] Add filters for orbit class, object type, source group, and catalog age.
- [ ] Add object detail overlay or status area.

### Phase 6: Tests And Validation

- [ ] Add parser tests for valid and malformed GP JSON/CSV.
- [ ] Add cache tests for stale, missing, and corrupt cache files.
- [ ] Add propagation tests for known element sets.
- [ ] Add render smoke coverage for `--host satview`.
- [ ] Verify Vulkan startup on Windows after renderer changes.
- [ ] Verify Metal startup on macOS after shader and depth path changes.
- [ ] Keep `python do.py smoke` passing.

### Phase 7: Production Hardening

- [ ] Add configurable data source group and refresh interval.
- [ ] Add clear error messages for network, parse, and cache failures.
- [ ] Avoid excessive memory growth with large catalogs and long track histories.
- [ ] Add documentation for data-source limits and privacy expectations.
- [ ] Consider optional Space-Track credentials only after public CelesTrak flow is solid.

## Current Branch Notes

The initial MVP is implemented on `codex/satview-module-depth-fix` in commit `edd106e`:

- New `satview` module and host integration.
- Vulkan and Metal render paths.
- Texture-mapped Earth and preview orbit rings.
- Renderer depth fix needed for solid 3D surfaces in custom passes.

The next meaningful work item is Phase 2: real catalog fetch/cache and a deterministic sample fixture.

## References

- CelesTrak current GP element sets: https://celestrak.org/NORAD/elements/
- CelesTrak GP data format documentation: https://celestrak.org/NORAD/documentation/gp-data-formats.php
- Space-Track documentation: https://www.space-track.org/documentation
- Solar System Scope 8k Earth texture maps: https://www.solarsystemscope.com/textures/
- Creative Commons Attribution 4.0 International: https://creativecommons.org/licenses/by/4.0/
