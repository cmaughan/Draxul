# SatView Planet Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve SatView planet displays with layered Saturn rings, visible moon bodies in planet views, Sun-view planet track checkboxes, and basic moon selection/navigation.

**Architecture:** Keep solar-system body facts in `satview_solar_system.*`, add small renderer-facing records to `SatViewScenePass`, and let `SatViewHost` build those records from existing body/orbit helpers. Use config-backed planet-track state so ImGui controls, rendering, and persistence share one model.

**Tech Stack:** C++20, ImGui, GLM, SatView Metal/Vulkan scene pass code, Catch2 unit tests.

---

### Task 1: Configurable Sun-View Planet Tracks

**Files:**
- Modify: `modules/satview/draxul-satview/include/draxul/satview/satview_config.h`
- Modify: `modules/satview/draxul-satview/src/satview_config.cpp`
- Modify: `modules/satview/draxul-satview/include/draxul/satview/satview_host.h`
- Modify: `modules/satview/draxul-satview/src/satview_host.cpp`
- Test: `tests/satview_config_tests.cpp`

- [ ] Add a failing config test that disables one planet track, stores config, reloads it, and expects the setting to survive.
- [ ] Add `SatViewPlanetTrackConfig` with eight booleans defaulting to true to preserve the existing all-planet Sun orbit guide behavior.
- [ ] Parse and serialize `planet_track_<name>` keys in the existing `[satview]` table.
- [ ] Mirror config into `SatViewHost` and mark the track buffer dirty when a checkbox changes.
- [ ] Run the targeted config tests and verify they pass.

### Task 2: Planet Track Vertex Filtering

**Files:**
- Modify: `modules/satview/draxul-satview/src/satview_host.cpp`
- Test: `tests/satview_solar_system_tests.cpp`

- [ ] Add a failing helper-level test proving selected Sun child tracks generate vertices only for enabled planets.
- [ ] Replace the Sun-view Earth-only track builder with a generic child-body orbit builder filtered by the new config.
- [ ] Keep Earth/Moon legacy track controls working outside generic solar-system Sun view.
- [ ] Run the targeted SatView solar-system tests and verify they pass.

### Task 3: Moon Body Records And Selection Mapping

**Files:**
- Modify: `modules/satview/draxul-satview/src/satview_solar_system.h`
- Modify: `modules/satview/draxul-satview/src/satview_solar_system.cpp`
- Modify: `modules/satview/draxul-satview/include/draxul/satview/satview_host.h`
- Modify: `modules/satview/draxul-satview/src/satview_host.cpp`
- Test: `tests/satview_solar_system_tests.cpp`

- [ ] Add failing tests for natural-satellite body records in a planet view and screen-pick mapping to a `SatViewCameraPov`.
- [ ] Add a small body-instance record that contains POV id, position in focus radii, radius in focus radii, rotation, polar ratio, and emissive state.
- [ ] Have the host build child body instances for generic body views from `satview_child_bodies`.
- [ ] Add single-click selection state for natural bodies and double-click POV navigation.
- [ ] Run the targeted SatView solar-system tests and verify they pass.

### Task 4: Layered Saturn Ring Disks

**Files:**
- Modify: `modules/satview/draxul-satview/src/satview_scene_pass.h`
- Modify: `modules/satview/draxul-satview/src/satview_render_vk.cpp`
- Modify: `modules/satview/draxul-satview/src/satview_render_metal.mm`
- Modify: `modules/satview/draxul-satview/shaders/satview_scene.metal`
- Modify: `modules/satview/draxul-satview/shaders/satview_*.vert` or `satview_*.frag` only if Vulkan needs a new shader path
- Test: `tests/satview_solar_system_tests.cpp`

- [ ] Add a failing test for Saturn ring-band metadata: several annulus bands, ordered inner/outer radii, and at least one darker gap band.
- [ ] Add a scene-pass ring record for layered annulus bands.
- [ ] Render the bands in Metal and Vulkan as transparent flattened disks in Saturn's local equatorial plane.
- [ ] Disable the old Saturn line-ring approximation when disk rings are available.
- [ ] Run the targeted SatView solar-system tests and verify they pass.

### Task 5: Docs And Validation

**Files:**
- Modify: `docs/features.md`

- [ ] Update SatView feature docs to mention layered Saturn disk rings, planet-track controls, moon geometry in body views, and natural-body selection.
- [ ] Build `draxul` and `draxul-tests`.
- [ ] Run `ctest` for SatView/Catch2 coverage.
- [ ] Run `py do.py smoke`.
- [ ] Commit the complete implementation only after validation passes.
