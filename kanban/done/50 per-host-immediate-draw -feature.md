# Per-Host Immediate Draw Architecture

## Summary

Invert the render loop so App controls draw order, not the renderer. Each host draws its own content immediately when told to — grid, 3D, ImGui, whatever — rather than the renderer batching everything by type in `end_frame()`.

## Motivation

The renderer currently decides draw order internally: all grid handles → all 3D passes → all ImGui → topmost overlays. This is the wrong abstraction — the `GridDrawLayer` enum was added as a workaround so the command palette could draw after MegaCity's 3D scene, but it couples the renderer to host ordering policy. The App should be the one deciding "terminal first, MegaCity second, palette last."

## Target Frame Loop

```
App::render_frame()
  renderer->begin_frame()   // acquire drawable, set up encoder
    terminal_host.draw()    // draws its grid
    megacity_host.draw()    // draws 3D scene + ImGui panels
    palette_host.draw()     // draws its grid on top of everything
  renderer->end_frame()     // present
```

## Key Changes

### New: `IFrameContext` — draw surface abstraction

Created by `begin_frame()`, valid until `end_frame()`. Wraps the platform encoder/command buffer. Three primitives:

- `draw_grid_handle(IGridHandle&)` — BG+FG instanced draws for a grid handle
- `record_render_pass(IRenderPass&, viewport)` — 3D pass (prepass + main)
- `render_imgui(ImDrawData*, ImGuiContext*)` — ImGui draw data

### New: `IHost::draw(IFrameContext&)`

Each host overrides to draw its content. App calls these in order between begin/end frame.

### Per-handle GPU buffers

Each grid handle owns its own GPU buffer instead of sharing one packed buffer. Eliminates global `upload_dirty_state()` and `baseInstance` offset tracking.

### Remove: `I3DRenderer` registration model

No more `register_render_pass()` / `unregister_render_pass()` / `set_3d_viewport()`. Hosts call `frame_ctx.record_render_pass()` directly in `draw()`.

### Remove: `add_host_imgui_draw_data()` queuing

Hosts render ImGui directly in `draw()` via `frame_ctx.render_imgui()`. Keep `IImGuiHost` for lifecycle only.

### Remove: `GridDrawLayer`, `I3DHost`, `IGridHost`

Draw order is App's responsibility. No need for layer enum or 3D attachment hooks.

### Prepass: lazy encoder creation

`begin_frame()` does NOT create the main render encoder. `IFrameContext` lazily creates it on first grid draw or 3D main-pass. Prepasses (which create their own encoders) run before this point.

## Implementation Steps

- [x] Add `IFrameContext` interface and `IHost::draw()` (non-breaking additions)
- [x] Per-handle GPU buffers (each handle owns its own MTLBuffer / VkBuffer)
- [x] Implement `MetalFrameContext` and `VkFrameContext` (ordered frame command recording + replay)
- [x] Change `begin_frame()` to return `IFrameContext*`
- [x] Wire host `draw()` implementations (GridHostBase, MegaCityHost, CommandPaletteHost)
- [x] Rewrite `App::render_frame()` to per-host draw loop
- [x] Remove legacy code (GridDrawLayer, I3DRenderer, render_passes_, host_imgui_draw_datas_, I3DHost)
- [x] Update test fakes

## Status

The app-owned per-host draw ordering is implemented and validated. Both Metal and Vulkan grid handles now own their own per-frame GPU buffers and descriptor/binding state, so host grid submission no longer depends on renderer-global packed storage or `baseInstance` offsets.

## Files

| File | Change |
|------|--------|
| `libs/draxul-renderer/include/draxul/base_renderer.h` | Add `IFrameContext`, change `begin_frame()`, remove `I3DRenderer` |
| `libs/draxul-renderer/include/draxul/renderer.h` | Remove `GridDrawLayer`, add `draw()` to IGridHandle |
| `libs/draxul-renderer/src/metal/metal_renderer.mm` | `MetalFrameContext`, per-handle buffers, lazy encoder |
| `libs/draxul-renderer/src/metal/metal_renderer.h` | Remove shared grid buffers, render_passes_ |
| `libs/draxul-renderer/src/vulkan/vk_renderer.cpp` | Parallel Metal changes |
| `libs/draxul-renderer/src/vulkan/vk_renderer.h` | Parallel Metal changes |
| `libs/draxul-host/include/draxul/host.h` | Add `draw()`, remove `I3DHost`, `IGridHost` |
| `libs/draxul-host/src/grid_host_base.cpp` | Implement `draw()` |
| `libs/draxul-megacity/src/megacity_host.cpp` | Implement `draw()`, remove attach_3d_renderer |
| `app/app.cpp` | Rewrite `render_frame()` |
| `app/command_palette_host.cpp` | Implement `draw()`, remove `set_draw_layer` |
| `app/host_manager.cpp` | Remove I3DHost post-init wiring |
| `tests/support/fake_renderer.h` | Update fakes |
| `tests/support/fake_grid_pipeline_renderer.h` | Update fakes |

## Detailed Plan

See `plans/transient-finding-duckling.md` for the full architectural plan with design rationale and edge cases.
