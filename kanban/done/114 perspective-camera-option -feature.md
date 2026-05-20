# 114 — Perspective Camera Option

## Summary

Add a UI combo box to switch between orthographic (current default) and perspective projection for the MegaCity view. The camera keeps its orbit/pan/zoom controls but the projection matrix changes, giving depth-based size variation and vanishing-point perspective.

## Motivation

The isometric view is great for overview and precision, but a perspective view would give a more immersive, game-like feel and make it easier to judge depth and spatial relationships — especially for large codebases with many modules.

## Scope

- Combo box in the MegaCity ImGui panel: `Orthographic` | `Perspective` (default: Orthographic)
- Persist the choice in config
- All existing interactions (orbit, pan, zoom, pick, double-click, tooltip hover) must work in both modes

## Implementation Plan

### 1. Camera — `isometric_camera.h/cpp` (moderate)

The `IsometricCamera` class currently hardcodes `glm::orthoRH_ZO` in `proj_matrix()`. Changes:

- Add a `ProjectionMode` enum (`Orthographic`, `Perspective`) and a setter
- `proj_matrix()` returns either `glm::orthoRH_ZO(...)` or `glm::perspectiveRH_ZO(fov, aspect, near, far)`
- **Zoom semantics differ**: ortho changes `ortho_half_height_`; perspective should change `orbit_radius_` (dolly) or FOV. Dolly is more intuitive — zoom in = move camera closer, not narrower FOV. Map the existing `zoom_by()` log-delta to orbit radius adjustment in perspective mode.
- `pan_delta_for_screen_drag()` currently divides by `ortho_half_height_` to get world-units-per-pixel. In perspective mode this depends on distance to the target: `world_units_per_pixel ≈ 2 * orbit_radius * tan(fov/2) / viewport_height`. Needs a perspective-aware path.
- `visible_ground_footprint()` unprojects NDC corners — this already works generically via `inv_view_proj` so it should just work with a perspective proj matrix.

**Tricky part**: The current zoom range (`min/max_ortho_half_height_`) needs a parallel concept for perspective (min/max orbit radius, or min/max FOV). Getting the zoom feel right so it doesn't clip through the ground or fly to infinity will need tuning.

### 2. Picking — `city_picking.cpp` (easy)

Picking already unprojects near/far NDC points through `inv(proj * view)` to get a ray origin and direction (lines 370-383). This is projection-agnostic — **should just work** with a perspective matrix. The ray will diverge from camera instead of being parallel, which is correct for perspective picking.

No changes expected, but needs testing.

### 3. Shadow Cascades — `shadow_cascade.cpp` (moderate)

`build_cascade()` unprojects NDC corners of each cascade split via `camera.inv_view_proj`. This is also projection-agnostic in principle, but:

- **Ortho frustum** corners form a rectangular box — the bounding sphere is tight.
- **Perspective frustum** corners form a truncated pyramid — the bounding sphere is much larger for far cascades, which means the shadow map covers more world area and shadow quality drops.
- The `kCascadeSplitDepths` (0.12, 0.32, 1.0) are tuned for ortho depth distribution. Perspective has non-linear depth, so splits may need re-tuning (logarithmic cascade splitting is standard for perspective).
- **Texel snapping** logic to prevent shadow shimmer should still work but may need a larger quantization step.

**Tricky part**: Shadow quality will likely degrade noticeably in perspective mode without adjusting cascade split ratios. May want to compute splits using the PSSM (Practical Split Scheme Mapping) lambda formula: `split = lerp(near * (far/near)^(i/n), near + (far-near)*i/n, lambda)`.

### 4. Scene Snapshot Builder — `scene_snapshot_builder.cpp` (easy)

Sets `scene.camera.view` and `scene.camera.proj` from the camera. Since these are just matrices, no change needed — the camera's `proj_matrix()` already returns the right one.

The `scene_far_ndc` computation (tightest NDC depth of visible buildings) is projection-agnostic.

### 5. Label Fade / Billboard — shaders (minor)

The `label_fade_px` uniform controls sign label fade based on screen-space size. In ortho, screen size is constant regardless of distance. In perspective, distant labels shrink naturally. The fade thresholds may need adjustment, or the shader could compute screen-space size from the perspective projection to keep labels readable.

### 6. Input Handling — `city_input_state.cpp` (easy)

Orbit (`orbit_target`) and pitch (`adjust_pitch`) are already angular — no change needed. Pan uses `pan_delta_for_screen_drag` which will be updated in the camera (step 1). Zoom dispatches to `zoom_by` which will be updated in the camera.

### 7. Tooltip Hover Ray — `megacity_host.cpp:1813` (easy)

Same `inv(proj * view)` unproject pattern as picking. Should just work.

### 8. Config — `megacity_code_config.h/cpp` (easy)

- Add `ProjectionMode projection_mode = ProjectionMode::Orthographic`
- Serialize/deserialize
- Add ImGui combo box in `ui_treesitter_panel.cpp`

## Risk Summary

| Area | Risk | Notes |
|------|------|-------|
| Camera zoom/pan | Low-medium | Different zoom semantics, needs tuning |
| Picking | Low | Already ray-based via inv_vp unproject |
| Shadows | Medium | Cascade quality degrades; may need PSSM splits |
| Labels | Low | May need fade threshold adjustment |
| Input | Low | Delegates to camera |
| AO | Low | Screen-space, projection-agnostic |

## Tasks

- [x] Add `ProjectionMode` enum and config field
- [x] Update `IsometricCamera::proj_matrix()` for perspective path
- [x] Update `zoom_by()` — dolly (orbit radius) in perspective mode
- [x] Update `pan_delta_for_screen_drag()` for perspective
- [x] Add ImGui combo box for projection mode
- [x] Test picking in perspective mode
- [x] Test shadow cascade quality in perspective; tune splits if needed
- [x] Test label fade in perspective; adjust thresholds if needed
- [x] Persist projection mode in config
