# SatView HDR Rendering Pipeline Plan

## Goal

Move SatView from direct LDR rendering into the application render pass to the same color pipeline used by Megacity:

```text
linear SatView scene
    -> RGBA16F scene target + D32 depth (4x, 2x, or 1x MSAA)
    -> single-sample RGBA16F resolve
    -> ACES tone map using exposure and white point
    -> BGRA8 sRGB target (hardware sRGB encoding)
    -> BGRA8 UNORM alias sampled into the shared application backbuffer
    -> ImGui UI in the normal application pass
```

This adds HDR scene composition and inspection without importing Megacity's G-buffer, ambient occlusion, or shadow passes.

## Important Terminology

The `RGBA16F` resource is a 16-bit floating-point **per channel** offscreen scene target, not a native HDR window/swapchain. The shared Vulkan swapchain and Metal drawable remain the existing 8-bit application backbuffer. This is also how Megacity works.

The final color-space boundary must remain explicit:

1. Scene shaders and blending operate in linear light and may produce values above `1.0`.
2. The ACES pass maps HDR linear values into linear display-range values.
3. Writing those values to `BGRA8_SRGB` lets the attachment perform the one required linear-to-sRGB encoding.
4. The same bytes are sampled through a `BGRA8_UNORM` alias for final composition, preventing an unwanted sRGB decode/encode cycle.

Do not change the shared swapchain/drawable format as part of this work. That would affect every host and ImGui.

## Architecture Decision

Keep the offscreen targets, render passes/encoders, pipelines, descriptors, and debug texture registration owned by `SatViewScenePass`.

- SatView must not depend on `modules/megacity/`, because both are independently optional product modules.
- Copy the proven Megacity pass sequence and color-space contract, but do not move the entire implementation into `draxul-renderer` during this feature.
- Use SatView-specific post/present shaders because its uniforms and shader packaging differ from CodeViz.
- Keep matching Vulkan and Metal implementations behind the existing `SatViewScenePass` API.
- Consider a shared renderer HDR helper only after both implementations have stabilized and their genuinely common API is evident.

MSAA is part of the required pipeline. Request 4x samples, fall back to 2x when 4x is unsupported for both `RGBA16F` color and `D32F` depth, and fall back to 1x only when neither multisampled mode is available. Apply the same negotiation policy on Vulkan and Metal, and expose the selected sample count in the debug panel.

## Render-Pass Split

### `record_prepass()`

This method runs before the shared application render pass and will own all SatView scene work:

1. Ensure scene textures, dynamic geometry buffers, and cloud uploads.
2. Select the highest mutually supported `RGBA16F` color and `D32F` depth sample count from 4x, 2x, and 1x.
3. Ensure one target set per buffered frame slot at the current pane dimensions and selected sample count.
4. Render every SatView scene layer into the multisampled `RGBA16F` target with SatView-owned multisampled `D32` depth:
   - starfield
   - Earth or Moon map/globe/ground surface
   - Moon
   - clouds
   - atmosphere
   - orbit tracks
   - satellite markers
5. Resolve multisampled HDR color into a single-sample `RGBA16F` texture. In the 1x fallback, render directly into that single-sample texture.
6. When the HDR debug panel is enabled, preserve the multisampled color attachment and generate an MSAA sample-difference image before it is discarded.
7. Run a fullscreen ACES pass from the resolved `RGBA16F` image into the `BGRA8_SRGB` final target.

All existing draw ordering and blend modes stay intact. Additive stars then accumulate in floating point instead of clipping in the 8-bit target. Resolve must happen before tone mapping because ACES is nonlinear; tone-mapping individual samples before averaging would produce incorrect edge energy.

### `record()`

This method stays inside the shared application render pass and becomes a presentation-only pass:

1. Select the current frame slot's `BGRA8_UNORM` alias view.
2. Draw a fullscreen triangle into the host's absolute pane viewport/scissor.
3. Leave depth testing and blending disabled for the composite.

ImGui is rendered afterward by the existing host path, so UI colors remain crisp and are not tone-mapped with the scene.

`SatViewScenePass::requires_main_depth_attachment()` can then return `false`; SatView owns its depth target.

## GPU Resources

Each buffered frame slot needs:

| Resource | Vulkan | Metal | Usage |
| --- | --- | --- | --- |
| MSAA HDR scene | `VK_FORMAT_R16G16B16A16_SFLOAT` | `MTLPixelFormatRGBA16Float` | 4x/2x color attachment; preserved only while debug inspection needs it |
| MSAA scene depth | `VK_FORMAT_D32_SFLOAT` | `MTLPixelFormatDepth32Float` | 4x/2x depth attachment |
| Resolved HDR scene | `VK_FORMAT_R16G16B16A16_SFLOAT` | `MTLPixelFormatRGBA16Float` | single-sample resolve target and tone-map input |
| Tone-mapped final | `VK_FORMAT_B8G8R8A8_SRGB` | `MTLPixelFormatBGRA8Unorm_sRGB` | single-sample color attachment, sampled |
| Final alias view | `VK_FORMAT_B8G8R8A8_UNORM` | `MTLPixelFormatBGRA8Unorm` | sampled during present/debug |
| MSAA difference | `VK_FORMAT_R8G8B8A8_UNORM` | `MTLPixelFormatRGBA8Unorm` | optional single-sample debug heat map |

The normal scene resources consume approximately 16 bytes per pixel at 1x, 36 bytes per pixel at 2x, and 60 bytes per pixel at 4x. At 4x this is about 119 MiB at 1080p, 211 MiB at 1440p, or 475 MiB at 4K **per buffered frame slot**, before the optional 4-byte-per-pixel debug image. Resize handling must release superseded targets promptly and avoid accidental duplicate target sets.

For the 1x fallback, do not allocate a redundant MSAA color image: render directly into the resolved HDR scene texture and use a single-sample depth target.

Target recreation requirements:

- Recreate when pane width, pane height, selected sample count, device, or buffered frame count changes.
- Remove Vulkan ImGui texture registrations before destroying their image views.
- Keep the last successfully rendered frame index for the debug panel.
- Never destroy resources still referenced by an in-flight frame. Follow the renderer's per-frame-slot lifetime/fence behavior used by Megacity.
- Log one actionable error and skip the pass if required formats or target creation fail.
- Record both the requested and active sample counts so a fallback is visible rather than silent.

## Vulkan Work

Update `modules/satview/draxul-satview/src/satview_render_vk.cpp` to introduce:

- A format-aware sample-count chooser that tests 4x, then 2x, then 1x against both `RGBA16F` color and `D32F` depth support.
- A per-frame `SatViewRenderTargets` record containing multisampled HDR/depth, resolved HDR, final sRGB/UNORM views, optional MSAA-difference output, allocations, framebuffers, and ImGui descriptors.
- A SatView scene render pass for multisampled `RGBA16F + D32` with a single-sample `RGBA16F` resolve attachment, plus a 1x variant that renders directly into the resolved texture.
- A tone-map render pass for `BGRA8_SRGB`.
- Scene pipelines compiled against the private HDR scene render pass instead of the shared application render pass.
- A tone-map descriptor/pipeline sampling the HDR scene.
- A present descriptor/pipeline sampling the UNORM alias and targeting `VkRenderContext::render_pass()`.
- Render-pass final layouts and descriptor updates that make HDR, depth, and final color legal shader reads.
- A debug-only fullscreen pass using `sampler2DMS`/`texelFetch` to visualize per-pixel differences between MSAA color samples. Use compatible regular and debug render passes so the multisampled color attachment can use `DONT_CARE` normally and `STORE` only while the debug panel is open.
- Complete cleanup/recreation for device changes, shared render-pass changes, resize, and shutdown.

The existing Earth/cloud texture helper should accept a color-space classification. Earth day/night, Moon, and cloud color textures should use sRGB formats so sampling returns linear RGB; alpha remains linear. Avoid manual gamma correction in the scene shaders.

## Metal Work

Update `modules/satview/draxul-satview/src/satview_render.mm` in parallel with Vulkan:

- Select 4x, 2x, or 1x by checking `[MTLDevice supportsTextureSampleCount:]` in that order.
- Add one target set per buffered frame slot using the formats and selected sample count above.
- Change all SatView scene pipelines to multisampled `MTLPixelFormatRGBA16Float` plus `Depth32Float`, with direct single-sample rendering for the 1x fallback.
- Encode the SatView scene in a private render encoder during `record_prepass()`.
- Resolve the multisampled scene into the single-sample HDR texture before tone mapping.
- Encode the ACES fullscreen pass into `BGRA8Unorm_sRGB`.
- Create a `BGRA8Unorm` texture view for the final present and debug image.
- Add a present pipeline targeting the existing `BGRA8Unorm` drawable pass.
- When debug inspection is enabled, use `MTLStoreActionStoreAndMultisampleResolve` and generate the same sample-difference view with a `texture2d_ms` shader. Use the cheaper resolve-only store action otherwise.
- Mark color assets as `RGBA8Unorm_sRGB` and keep alpha/data semantics linear.
- Keep target recreation and current-frame selection behavior aligned with Vulkan.

Add the post and present functions to `shaders/satview_scene.metal` so SatView still ships one module-specific metallib.

## Shaders And Color Contract

Add Vulkan shaders:

- `shaders/satview_post.vert`: fullscreen triangle and UV generation.
- `shaders/satview_post.frag`: the same ACES approximation used by Megacity, parameterized by exposure and white point.
- `shaders/satview_present.frag`: unmodified texture sample for the encoded final image.
- `shaders/satview_msaa_debug.frag`: read 2 or 4 samples from the multisampled HDR input, compute maximum color/luminance disagreement, and emit a clearly scaled heat map. For 1x, emit black and report that MSAA is inactive in the panel.

Update the shader build/staging rules in `cmake/CompileShaders.cmake` or the current Vulkan shader manifest. The Metal functions remain in `shaders/satview_scene.metal` and its existing metallib target.

Audit SatView scene outputs under this contract:

- Remove LDR-only RGB clamps/saturation before the tone-map pass.
- Keep star brightness as a pre-tone-map emissive/radiance gain, independent of global exposure.
- Treat host-selected pastel/marker/track colors as sRGB display colors and convert them to linear once before writing to the HDR target.
- Let sRGB texture formats decode Earth, Moon, and cloud RGB automatically.
- Perform atmosphere, cloud, and surface blending in linear space.
- Do not add a manual `pow(..., 1/2.2)` after ACES; the sRGB attachment performs that encoding.

## Controls And Persistence

Extend `SatViewConfig` and its load/store tests with:

- `tone_map_exposure`, default `1.32`, range `0.0 .. 8.0`.
- `tone_map_white_point`, default `0.9`, range `0.5 .. 32.0`.
- `show_hdr_debug_panel`, default `false`.

Add a `Tone Mapping` subsection to SatView's Visuals UI with the same exposure and white-point ranges as Megacity. Include these values in `Reset Defaults` and feed them to `SatViewScenePass` every frame.

Keep `Star brightness` as a separate control:

- Star brightness changes star radiance relative to Earth, Moon, and atmosphere.
- Exposure changes the entire HDR scene before tone mapping.
- White point changes highlight compression/rolloff.

## HDR Buffer Debug Panel

Add `SatViewScenePass::render_hdr_debug_ui()` and call it while building the SatView ImGui frame when `show_hdr_debug_panel` is enabled.

The panel should be named `SatView HDR Buffers` and show only resources SatView actually owns:

- `MSAA Difference`: a generated heat map that is dark where all samples agree and bright at pixels whose samples differ. This makes edge coverage visible without requiring ImGui to sample a multisampled texture directly.
- `Scene HDR Resolved`: the single-sample `RGBA16F` resolve. Like Megacity's current preview, values above display range will clip in the ImGui thumbnail; this view is diagnostic, not a second tone-map path.
- `Scene Final`: the tone-mapped sRGB image sampled through the UNORM alias.
- Resource dimensions, requested sample count, active sample count, fallback status, and current exposure/white-point values.

Use a two-column responsive table like Megacity's G-buffer panel. Register Vulkan textures lazily and remove registrations during target teardown. Metal can pass texture objects directly to ImGui.

The panel verifies both configuration and visible effect: the active sample count proves which fallback was selected, the difference heat map proves that samples differ along covered edges, and the resolved/final pair proves that resolve occurs before tone mapping.

Do not expose AO, normals, materials, or shadow placeholders in SatView; those buffers do not exist.

## Implementation Sequence

1. Add configuration fields, clamping, persistence, host state, reset behavior, and UI controls with focused unit tests.
2. Add SatView post/present/MSAA-debug shaders and cross-platform shader build wiring.
3. Add adaptive 4x/2x/1x negotiation and expose the active result through `SatViewScenePass` debug state.
4. Implement Vulkan target ownership, multisampled private scene pass, HDR resolve, tone-map pass, debug-difference pass, and final composite.
5. Implement the equivalent Metal target/encoder/pipeline path and fallback negotiation.
6. Change color textures and shader inputs to obey the linear/sRGB contract; calibrate defaults only after removing double-gamma risks.
7. Add the HDR buffer debug panel on both backends.
8. Update `docs/features.md` and `assets/satview/README.md` with the controls, active-MSAA reporting, and color-pipeline behavior.
9. Run the full validation matrix and inspect all three SatView projections.

Keep each backend compiling throughout the work. The Vulkan and Metal changes should land together as one user-facing feature.

## Validation

### Automated

- Build `draxul` and `draxul-tests` in Release.
- Run the focused SatView config tests.
- Run `python do.py smoke`.
- Run `ctest --test-dir build --build-config Release --output-on-failure`.
- Run the render smoke/snapshot suite and inspect any changed reference before blessing it.
- Verify shader compilation and staging with `DRAXUL_ENABLE_SATVIEW=ON`.
- Configure/build once with `DRAXUL_ENABLE_SATVIEW=OFF` to catch accidental unconditional shader/module wiring.

### Visual And Runtime

- Start `--host satview` with Vulkan validation enabled and confirm no image-layout, descriptor-lifetime, or render-pass compatibility errors.
- Exercise resize and split-pane viewport changes repeatedly.
- Check Globe, Map, and Ground projections with both Earth and Moon POV where applicable.
- Confirm a 4x-capable device reports and uses 4x; force/test the 2x and 1x selection branches through focused sample-selection tests or a temporary capability override.
- Confirm the MSAA difference view lights up along the Earth/Moon silhouettes, tracks, markers, and star-quad edges while remaining dark in uniform areas.
- Confirm the resolved HDR image is stable when the debug panel is opened or closed; preserving the multisampled attachment for inspection must not alter scene output.
- Confirm stars can exceed `1.0` in the HDR buffer and roll off smoothly rather than clipping to flat white.
- Confirm exposure affects the full scene while star brightness affects stars only.
- Confirm Earth/Moon/cloud textures do not receive double gamma and day/night colors agree between Vulkan and Metal.
- Confirm the HDR debug window survives resize, close/reopen, and host shutdown.
- On macOS, launch the Metal build and compare the same control values and scene against Vulkan. If macOS hardware is unavailable locally, require CI compilation and explicitly leave runtime parity as outstanding.

## Acceptance Criteria

- Every SatView scene layer renders into a linear multisampled `RGBA16F` target before resolve and tone mapping, with a working 4x -> 2x -> 1x fallback.
- ACES exposure and white-point controls are persisted, resettable, and visually functional.
- Hardware sRGB encoding occurs exactly once between tone mapping and display.
- SatView no longer requires the shared main depth attachment.
- The application UI is composited after tone mapping and remains unchanged.
- The debug panel reports the selected MSAA mode and displays sample difference, resolved HDR, and final color on Vulkan and Metal.
- No Megacity module dependency is introduced.
- All automated validation passes and both backend implementations remain source-compatible.

## Follow-Up Opportunities

- Add a user-facing quality control only if profiling shows a practical need to override the automatic highest-supported MSAA selection.
- Add histogram/false-color/nit-range debug views once the basic HDR path is stable.
- If a third product host needs this exact pipeline, extract the now-proven target/tone-map/present lifecycle into a renderer-owned helper instead of maintaining a third copy.
