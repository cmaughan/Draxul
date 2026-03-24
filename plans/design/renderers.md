# Draxul 3D Architecture & MegaCity Analysis

## Layers of the 3D System

The rendering is organized as a **three-tier interface hierarchy**:

```
IBaseRenderer          → lifecycle (init, shutdown, begin/end frame, resize)
  └─ I3DRenderer       → adds register_render_pass(shared_ptr<IRenderPass>), set_3d_viewport()
       └─ IGridRenderer → adds grid-specific: create_grid_handle(), atlas, cell metrics
```

Both `VkRenderer` and `MetalRenderer` implement `IGridRenderer` and transitively satisfy all tiers. The key abstraction for 3D is `IRenderPass` — a single-method interface:

```cpp
class IRenderPass {
    virtual void record(IRenderContext& ctx) = 0;
};
```

Any subsystem registers a pass with `I3DRenderer::register_render_pass()`, and the renderer calls `record()` each frame inside the command buffer recording, after grid drawing but before ImGui.

## Host Hierarchy

```
IHost (base)
  └─ I3DHost (adds attach_3d_renderer/detach_3d_renderer)
       ├─ IGridHost → GridHostBase → NvimHost, LocalTerminalHost (terminal hosts)
       └─ MegaCityHost (3D scene, NOT a grid host)
```

`MegaCityHost` inherits `I3DHost` directly — it has no grid, no terminal. The `HostManager` does a one-shot `dynamic_cast<I3DHost*>` at startup and wires the renderer.

## MegaCity: How Meshes Are Created

All geometry is **procedural**, generated in `libs/draxul-megacity/src/mesh_library.h/cpp`:

- **`build_unit_cube_mesh()`** — 24 vertices (6 faces x 4), 36 indices. Standard unit cube from -0.5 to 0.5.
- **`build_outline_grid_mesh(FloorGridSpec)`** — Dynamically generated grid lines culled to the camera's visible ground footprint. Each line is a thin quad. Regenerated when the viewport changes.

Meshes are uploaded to GPU via VMA (Vulkan) as host-visible mapped buffers with `memcpy` + flush — no staging. The scene is a **flat list of `SceneObject`** structs (mesh ID + world matrix + color), not a scene graph.

**Scene snapshot each frame:**
```
MegaCityHost::build_scene_snapshot()
  → camera view/proj matrices from IsometricCamera (orthographic, glm::orthoRH_ZO)
  → frustum-based floor grid culling
  → SceneObject per GridObject in IsometricWorld (currently a 5x5 grid, single cube)
```

**Rendering:** `IsometricScenePass::record()` binds the pipeline, uploads frame uniforms (view/proj/light_dir), then for each object: push constants (world matrix + color) → `vkCmdDrawIndexed`. Simple per-vertex Lambertian lighting (20% ambient + 80% diffuse) in the vertex shader.

## Grid Rendering: Highly Efficient

Both backends use the **same instanced rendering strategy**:

- **No vertex or index buffers for quads** — 6 vertices are hardcoded in the shader and generated procedurally per instance
- **One draw call per pass** — background pass draws ALL cells as one `vkCmdDraw(6, cell_count, 0, offset)`, foreground pass does the same
- **SSBO-indexed** — `gl_InstanceIndex` / `instance_id` reads cell data directly from the storage buffer
- **GpuCell is 112 bytes** (pos, size, bg/fg/sp colors, atlas UVs, glyph offset/size, style flags)

**Vulkan:** Single shared SSBO for all grid handles, offset-based. Host-visible coherent memory via VMA, direct `memcpy` + flush. 2 frames in flight with explicit fences/semaphores.

**Metal:** Per-handle `MTLBuffer` with `StorageModeShared`. Direct writes, no explicit flush needed. Single dispatch semaphore for frame pacing.

**Atlas:** 2048x2048 RGBA8 texture, shelf-packed. Vulkan uses staging buffer + `vkCmdCopyBufferToImage`; Metal uses direct `replaceRegion` (simpler).

For a 120x40 terminal: **2 draw calls total** (not 9,600). This is excellent.

## Is There an ECS?

**No.** Draxul uses a **classical composition-based OOP architecture**. No entities, components, systems, archetypes, or registries. The ownership graph is straightforward:

```
App (orchestrator)
├── Window (SdlWindow)
├── RendererBundle (VkRenderer or MetalRenderer)
├── TextService (FreeType + HarfBuzz + GlyphCache)
├── HostManager
│   ├── SplitTree (binary tree of panes)
│   └── map<LeafId, unique_ptr<IHost>>
├── InputDispatcher
├── UiPanel (ImGui)
└── Config (TOML)
```

The main loop is **event-driven/reactive** (not a fixed-timestep game loop). Rendering only happens when `frame_requested_` is true, with deadline-based `SDL_WaitEvent` for efficient idle.

## What's Clean

1. **Interface layering** — `IBaseRenderer → I3DRenderer → IGridRenderer` is well-designed. Grid hosts and 3D hosts compose naturally without knowing about each other.

2. **Render pass abstraction** — `IRenderPass::record(IRenderContext&)` is minimal and clean. Any subsystem can register a pass without touching the renderer internals.

3. **Instanced grid rendering** — O(1) draw calls regardless of grid size. Procedural quads in shaders, no vertex buffers. This is textbook efficient GPU usage.

4. **Threading discipline** — All grid/host state is main-thread-only (asserted). Reader thread communicates via thread-safe queue + SDL wake event. No locks in the hot path.

5. **Host/renderer decoupling** — Hosts don't know about Vulkan or Metal. The `IGridHandle` abstraction lets each pane own its GPU buffer independently.

6. **Dirty cell tracking** — Only changed cells are copied to GPU each frame, not the entire grid.

7. **Highlight deduplication** — `HighlightTable` maps attributes to 16-bit IDs. Cells store indices, not full attribute structs.

8. **Split tree layout** — Clean binary tree with recursive viewport computation. Hit-testing for focus is simple point-in-rect.

## What's Not Clean / Could Be Better

1. **Platform leakage in `IRenderContext`** — `native_command_buffer()` returns `void*`, and `IsometricScenePass::record()` immediately does `static_cast<VkRenderContext*>(&ctx)`. The abstraction leaks; the render pass is effectively platform-specific despite the interface.

2. **MegaCity is embryonic** — A 5x5 world with a single cube, flat object list, no scene graph, no batching. Every object is a separate `vkCmdDrawIndexed` with push constants. For the current scope (1 cube + grid lines) this is fine, but it won't scale to an actual "mega city" without instanced rendering for objects, indirect draws, or at least draw call batching.

3. **No Metal implementation for MegaCity 3D** — `megacity_render_vk.cpp` exists but there's no `megacity_render_metal.mm`. The `IsometricScenePass` casts directly to `VkRenderContext`, so it simply won't work on macOS. The abstraction boundary exists but isn't fulfilled.

4. **Buffer management asymmetry** — Vulkan packs all grid handles into one SSBO (efficient, fewer bindings); Metal creates one `MTLBuffer` per handle (simpler but more state changes). This inconsistency makes the behavior differ subtly across platforms.

5. **No object batching/culling for 3D** — No frustum culling, no spatial partitioning, no LOD. The floor grid does camera-frustum culling (good), but objects don't.

6. **Mesh lifetime management is manual** — `retired_grid_meshes` with deferred cleanup for frames-in-flight is correct but fragile. A proper frame-resource-manager or ring-buffer allocator would be cleaner.

7. **Single render pass registration** — `I3DRenderer` only supports one `IRenderPass` at a time (`register_render_pass` / `unregister_render_pass`). If you ever need multiple 3D passes (shadows, post-processing, transparency), this interface needs rework.

8. **Frame pacing difference** — Vulkan has 2 frames in flight; Metal has effectively 1 (single semaphore). The Metal path may have more CPU-GPU stalls on slower hardware.

## Bottom Line

The **grid rendering is production-quality efficient** — instanced draws, procedural quads, host-visible coherent buffers, dirty-cell-only updates. This is very well done for a terminal renderer.

The **3D/MegaCity layer is a clean prototype** — good abstractions at the interface level, but the implementation is Vulkan-only, single-object draw calls, no batching, and a flat scene model. It's architected to scale (the `IRenderPass` pattern is extensible) but hasn't needed to yet.

---

## Terminal Grid Rendering: Full Pipeline

### Screen Size to Grid Dimensions

In `app/app.cpp`, `viewport_from_descriptor()`:

```cpp
const int usable_w = pixel_size.x - 2 * padding;
const int usable_h = pixel_size.y - 2 * padding;
grid_cols = cell_w > 0 ? std::max(1, usable_w / cell_w) : 1;
grid_rows = cell_h > 0 ? std::max(1, usable_h / cell_h) : 1;
```

Cell size comes from `FontMetrics` (FreeType): `cell_width` = monospace advance, `cell_height` = ascender + descender + leading. The app calls `renderer.set_cell_size()` and `set_ascender()` after loading fonts.

Window resize triggers: `on_resize()` → `renderer.resize(pixel_w, pixel_h)` → `refresh_window_layout()` → `host_manager_.recompute_viewports()`.

### GpuCell Structure (112 bytes, alignas(16))

Defined in `libs/draxul-renderer/include/draxul/renderer_state.h`:

```
Offset  Size  Field            Purpose
  0      8    pos (vec2)       Screen-space pixel position (col*cell_w + padding, row*cell_h + padding)
  8      8    size (vec2)      Cell pixel dimensions
 16     16    bg (vec4)        Background color (normalized RGBA)
 32     16    fg (vec4)        Foreground color
 48     16    sp (vec4)        Special color (undercurl/decorations)
 64     16    uv (vec4)        Atlas texture coords (u0, v0, u1, v1)
 80      8    glyph_offset     Bearing from cell top-left to glyph
 88      8    glyph_size       Rendered glyph dimensions in pixels
 96      4    style_flags      Bitfield: BOLD=1, ITALIC=2, UNDERLINE=4, STRIKETHROUGH=8, UNDERCURL=16, COLOR_GLYPH=32
100     12    _pad[3]          Padding to 112 bytes
```

### Data Flow: Neovim RPC to GPU

```
Neovim grid_line RPC
  → Grid::set_cell() [marks cell dirty]
  → GridRenderingPipeline::flush()
      → grid.get_dirty_cells()
      → For each dirty cell:
          shape text (HarfBuzz) → glyph IDs
          rasterize glyph (FreeType) → bitmap
          atlas pack (shelf algorithm) → AtlasRegion {uv, bearing, size}
      → Build CellUpdate {col, row, bg, fg, sp, glyph, style_flags}
  → IGridHandle::update_cells(span<CellUpdate>)
      → RendererState::apply_update_to_cell():
          cell.pos   = {col * cell_w + padding, row * cell_h + padding}
          cell.size  = {cell_w, cell_h}
          cell.bg/fg/sp = colors from highlight resolution
          cell.uv    = atlas region UVs
          cell.glyph_offset = {bearing.x, cell_h - ascender + bearing.y}
          cell.glyph_size   = glyph pixel dimensions
          cell.style_flags  = bold|italic|underline|strikethrough|undercurl|color_glyph
```

### Buffer Layout in GPU Memory

**Vulkan** — all handles packed into **one shared SSBO** per frame-in-flight:
```
[Handle 0: cols×rows cells | 256 overlay cells | 1 cursor cell]
[Handle 1: cols×rows cells | 256 overlay cells | 1 cursor cell]
...
```

Each handle renders at an `instance_offset` into this buffer. Two SSBOs exist (one per frame-in-flight) with host-visible coherent memory via VMA — direct `memcpy` + `vmaFlushAllocation`, no staging buffer.

Upload in `VkRenderer::upload_dirty_state()`:
```cpp
size_t total_size = 0;
for (auto* handle : grid_handles_)
    total_size += handle->state_.buffer_size_bytes();

grid_buffer.ensure_size(allocator, total_size);
auto* mapped = static_cast<std::byte*>(grid_buffer.mapped());
size_t byte_offset = 0;
for (auto* handle : grid_handles_) {
    handle->state_.copy_to(mapped + byte_offset);
    byte_offset += handle->state_.buffer_size_bytes();
}
grid_buffer.flush_range(allocator, 0, total_size);
```

**Metal** — each handle owns its **own `MTLBuffer`** with `StorageModeShared` (CPU+GPU visible, coherent, no explicit flush needed).

### Vertex Shader: Procedural Quad Generation

No vertex or index buffers. The BG vertex shader (`shaders/grid_bg.vert`) generates 6 vertices per cell:

```glsl
layout(push_constant) uniform PushConstants {
    float screen_w, screen_h;
    float cell_w, cell_h;
    float scroll_offset_px;
    float viewport_x, viewport_y;
} pc;

struct Cell { /* 112 bytes matching GpuCell layout */ };
layout(set = 0, binding = 0) readonly buffer CellBuffer { Cell cells[]; };

void main() {
    Cell cell = cells[gl_InstanceIndex];

    // Hardcoded quad: 2 triangles, 6 vertices
    vec2 offsets[6] = vec2[](
        vec2(0, 0), vec2(1, 0), vec2(0, 1),   // triangle 1
        vec2(1, 0), vec2(1, 1), vec2(0, 1)    // triangle 2
    );
    vec2 offset = offsets[gl_VertexIndex];

    // Screen-space position in pixels
    vec2 pos = vec2(cell.pos_x, cell.pos_y) + offset * vec2(cell.size_x, cell.size_y);
    pos.y -= pc.scroll_offset_px;          // smooth scroll
    pos += vec2(pc.viewport_x, pc.viewport_y);  // pane offset

    // Pixel → NDC: [0, screen] → [-1, +1]
    vec2 ndc = (pos / vec2(pc.screen_w, pc.screen_h)) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);

    frag_bg = vec4(cell.bg_r, cell.bg_g, cell.bg_b, cell.bg_a);
    frag_fg = vec4(cell.fg_r, cell.fg_g, cell.fg_b, cell.fg_a);
    frag_sp = vec4(cell.sp_r, cell.sp_g, cell.sp_b, cell.sp_a);
    frag_local_uv = offset;        // [0,1] within cell (for decoration positioning)
    frag_style_flags = cell.style_flags;
}
```

The FG vertex shader (`shaders/grid_fg.vert`) is similar but positions a glyph sub-quad within the cell using `glyph_offset` and `glyph_size`, and interpolates atlas UVs for texture sampling.

Metal shaders (`shaders/grid.metal`) are identical in algorithm but use `instance_id [[instance_id]]` and apply `ndc.y = -ndc.y` for Metal's Y-up convention.

### Fragment Shaders

**Background fragment** (`shaders/grid_bg.frag`) — outputs solid background color with decoration overlays:

```glsl
void main() {
    vec4 color = frag_bg;
    vec4 accent = frag_sp.a > 0.0 ? frag_sp : frag_fg;

    if (underline && frag_local_uv.y >= 0.86 && frag_local_uv.y <= 0.93)
        color = accent;                              // bottom 7-14% band
    else if (strikethrough && frag_local_uv.y >= 0.48 && frag_local_uv.y <= 0.54)
        color = frag_fg;                             // middle 6% band
    else if (undercurl) {
        float baseline = 0.84 + 0.05 * sin(frag_local_uv.x * 6π);
        if (abs(frag_local_uv.y - baseline) <= thickness)
            color = accent;                          // sine wave near bottom
    }
    out_color = color;
}
```

Decoration positions are normalized to [0,1] within the cell via `frag_local_uv`. Constants shared between GLSL and Metal via `decoration_constants_shared.h`.

**Foreground fragment** (`shaders/grid_fg.frag`) — samples glyph from atlas:

```glsl
void main() {
    vec4 atlas_sample = texture(atlas, frag_uv);
    float alpha = atlas_sample.a;
    if (alpha < 0.01) discard;   // early out for empty space

    bool color_glyph = (frag_style_flags & STYLE_FLAG_COLOR_GLYPH) != 0u;
    out_color = color_glyph
        ? atlas_sample                                    // emoji: pass through RGBA
        : vec4(frag_fg.rgb, frag_fg.a * alpha);          // text: tint with fg color
}
```

### The Draw Calls

Per pane/handle, per frame, in `VkRenderer::record_command_buffer()`:

```cpp
uint32_t instance_offset = 0;
for (auto* handle : grid_handles_) {
    const int bg_instances = handle->state_.bg_instances();
    const int fg_instances = handle->state_.fg_instances();
    const uint32_t handle_span = handle->state_.buffer_size_bytes() / sizeof(GpuCell);

    // Scissor to pane pixel bounds
    vkCmdSetScissor(cmd, 0, 1, &pane_scissor);

    // Push constants: screen_w, screen_h, cell_w, cell_h, scroll_offset_px, viewport_x, viewport_y
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 28, push_data);

    // BG pass — opaque, no blending
    vkCmdBindPipeline(cmd, GRAPHICS, bg_pipeline);
    vkCmdBindDescriptorSets(cmd, GRAPHICS, bg_layout, 0, 1, &bg_desc_set, 0, nullptr);
    vkCmdDraw(cmd, 6, bg_instances, 0, instance_offset);

    // FG pass — alpha blended (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
    vkCmdBindPipeline(cmd, GRAPHICS, fg_pipeline);
    vkCmdBindDescriptorSets(cmd, GRAPHICS, fg_layout, 0, 1, &fg_desc_set, 0, nullptr);
    vkCmdDraw(cmd, 6, fg_instances, 0, instance_offset);

    instance_offset += handle_span;
}
```

- `vkCmdDraw(6, N, 0, offset)` — 6 vertices per quad (procedural), N instances (cells), offset into shared SSBO
- For 120×40 terminal: **2 draw calls** (BG + FG), each with ~4800 instances

### Descriptor Set Bindings

**BG descriptor set**: binding 0 = SSBO (GpuCell buffer, read-only, vertex+fragment stages)

**FG descriptor set**: binding 0 = SSBO (GpuCell buffer), binding 1 = combined image sampler (2048×2048 RGBA8 atlas, linear filtering, clamp-to-edge)

Descriptor sets updated when the buffer resizes or atlas changes. One set per frame-in-flight.

### Pipeline State

| Property | BG Pipeline | FG Pipeline |
|----------|-------------|-------------|
| Vertex input | None (procedural) | None (procedural) |
| Topology | Triangle list | Triangle list |
| Rasterization | Fill, no culling | Fill, no culling |
| Depth | Not used (z=0) | Not used (z=0) |
| Blending | Disabled (opaque) | SRC_ALPHA / ONE_MINUS_SRC_ALPHA |
| Viewport | Full framebuffer (dynamic) | Full framebuffer (dynamic) |
| Scissor | Dynamic, per-pane | Dynamic, per-pane |
| MSAA | None | None |

### Dirty Cell Optimization

Grid marks individual cells dirty on RPC events. `GridRenderingPipeline::flush()` reads only dirty cells and builds `CellUpdate` only for those. `RendererState` tracks which GpuCells changed. On upload, the full buffer is copied to the SSBO — the dirty tracking avoids redundant *processing* (text shaping, glyph rasterization) rather than redundant *transfer*, since the host-visible buffer is already the GPU's read source.

---

## MegaCity 3D Rendering: Full Pipeline

### Scene Data Structures

Defined in `libs/draxul-megacity/src/isometric_scene_types.h`:

```cpp
enum class MeshId { Cube, Grid };

struct SceneVertex {              // 36 bytes
    glm::vec3 position;           // 12 bytes
    glm::vec3 normal;             // 12 bytes
    glm::vec3 color;              // 12 bytes
};

struct MeshData {
    std::vector<SceneVertex> vertices;
    std::vector<uint16_t> indices;
};

struct SceneObject {
    MeshId mesh;
    glm::mat4 world;              // Object → world transform
    glm::vec4 color;              // Per-object color (modulates vertex color)
};

struct SceneCameraData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 light_dir;
};

struct FloorGridSpec {
    bool enabled;
    int min_x, max_x, min_z, max_z;   // Grid-cell range (frustum-culled)
    float tile_size, line_width, y;
    glm::vec4 color;
};

struct SceneSnapshot {
    SceneCameraData camera;
    FloorGridSpec floor_grid;
    std::vector<SceneObject> objects;
};
```

### Mesh Generation (Procedural)

All geometry is built in `libs/draxul-megacity/src/mesh_library.cpp`.

**Unit cube** — `build_unit_cube_mesh()`:
- 6 faces, 4 vertices each = **24 vertices**, **36 indices**
- Spans [-0.5, +0.5] on all axes
- Each face has its own normal (no shared vertices — flat shading)
- Index pattern per face: `{base, base+1, base+2, base, base+2, base+3}`
- All vertex colors = white (1,1,1) — actual color applied via push constants

Face layout:
```
Face 0 (Front,  Z+):  normal (0,0,+1)   verts at z=+0.5
Face 1 (Back,   Z-):  normal (0,0,-1)   verts at z=-0.5
Face 2 (Left,   X-):  normal (-1,0,0)   verts at x=-0.5
Face 3 (Right,  X+):  normal (+1,0,0)   verts at x=+0.5
Face 4 (Top,    Y+):  normal (0,+1,0)   verts at y=+0.5
Face 5 (Bottom, Y-):  normal (0,-1,0)   verts at y=-0.5
```

**Floor grid** — `build_outline_grid_mesh(FloorGridSpec)`:
- For each X line from `min_x` to `max_x`: creates a thin quad from `(world_x - half_width, y, zmin)` to `(world_x + half_width, y, zmax)`, normal (0,1,0)
- For each Z line from `min_z` to `max_z`: creates a thin quad from `(xmin, y, world_z - half_width)` to `(xmax, y, world_z + half_width)`, normal (0,1,0)
- Line width = `tile_size * 0.08` (8% of a tile)
- Grid sits at y = -0.001 (slightly below ground to avoid z-fighting with objects)
- Color from spec (default: light gray 0.62, 0.62, 0.66)
- Typical 5×5 grid: ~14 quads = **56 vertices**, **84 indices**

### GPU Buffer Management

Defined in `megacity_render_vk.cpp`. All buffers use VMA with host-visible coherent memory:

```cpp
VmaAllocationCreateInfo alloc_ci = {};
alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;
alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
               | VMA_ALLOCATION_CREATE_MAPPED_BIT;
```

**Mesh upload**: `memcpy` to mapped pointer + `vmaFlushAllocation`. No staging buffer.

```cpp
struct MeshBuffers {
    Buffer vertices;          // VkBuffer + VmaAllocation + mapped pointer
    Buffer indices;
    uint32_t index_count;
};
```

**Per-frame uniform buffer** (one per frame-in-flight):
```cpp
struct FrameUniforms {        // 144 bytes
    glm::mat4 view;           // 64 bytes
    glm::mat4 proj;           // 64 bytes
    glm::vec4 light_dir;      // 16 bytes
};

struct FrameResources {
    Buffer frame_uniforms;    // Host-visible mapped buffer
    VkDescriptorSet descriptor_set;
};
```

**Grid mesh lifecycle** — when camera pans and `FloorGridSpec` changes:
1. `same_grid_spec()` detects difference
2. Old mesh pushed to `retired_grid_meshes` with `reclaim_frame_index = (current + buffered_frames - 1) % buffered_frames`
3. New mesh generated from `build_outline_grid_mesh()` and uploaded
4. `reclaim_retired_meshes()` called each frame start — destroys when reclaim frame matches current frame (guarantees GPU is done)

### Isometric Camera

Defined in `libs/draxul-megacity/src/isometric_camera.h/cpp`.

```cpp
class IsometricCamera {
    glm::vec3 position_{ -6.0f, 7.0f, -6.0f };
    glm::vec3 target_{ 2.5f, 0.0f, 2.5f };
    glm::vec3 follow_offset_{ -5.0f, 6.25f, -5.0f };
    float ortho_half_height_ = 4.0f;
    float aspect_ = 1.0f;
};
```

**View matrix**: `glm::lookAtRH(position_, target_, {0,1,0})` — right-handed, Y-up.

**Projection matrix**: `glm::orthoRH_ZO(...)` — orthographic, right-handed, zero-to-one depth. With Vulkan Y-flip: `proj[1][1] *= -1.0f`.

**Frustum-based grid culling** — `visible_ground_footprint(plane_y)`:
1. Compute `inv_view_proj = inverse(proj * view)`
2. For each NDC corner `[-1,-1], [1,-1], [1,1], [-1,1]`:
   - Unproject to world near/far planes
   - Cast ray from near to far, intersect with `y = plane_y`
3. Take AABB of all intersection points
4. Round to grid cell boundaries (with 1 cell margin)
5. Result: `{min_x, max_x, min_z, max_z}` in grid coordinates

### Scene Assembly

In `MegaCityHost::build_scene_snapshot()`:

```cpp
SceneSnapshot scene;
scene.camera.view = camera_->view_matrix();
scene.camera.proj = camera_->proj_matrix();
scene.camera.light_dir = normalize(vec4(-0.5, -1.0, -0.3, 0.0));

// Floor grid from frustum
GroundFootprint footprint = camera_->visible_ground_footprint(0.0f);
scene.floor_grid = {
    enabled: true,
    min_x: floor(footprint.min_x / tile_size) - 1,
    max_x: ceil(footprint.max_x / tile_size) + 1,
    min_z: floor(footprint.min_z / tile_size) - 1,
    max_z: ceil(footprint.max_z / tile_size) + 1,
    tile_size: 1.0, line_width: 0.08,
    y: -0.001,  // z-fighting avoidance
    color: vec4(0.62, 0.62, 0.66, 1.0)
};

// Objects
for (const auto& obj : world_->objects()) {
    vec3 pos = grid_to_world(obj.x, obj.y, obj.elevation);
    // grid_to_world: { (x+0.5)*tile_size, elevation, (y+0.5)*tile_size }
    scene.objects.push_back({
        MeshId::Cube,
        translate(mat4(1.0), pos + vec3(0, 0.5, 0)),  // raise cube by half-height
        vec4(obj.color, 1.0)
    });
}
```

Currently: one cube at grid center (2,2), color tan/brown (0.85, 0.55, 0.30).

### Vulkan Pipeline

Created lazily in `State::ensure(VkRenderContext)`:

**Vertex input**:
- 1 binding, stride = `sizeof(SceneVertex)` = 36 bytes, per-vertex
- 3 attributes: position (vec3, offset 0), normal (vec3, offset 12), color (vec3, offset 24)

**State**:
| Property | Value |
|----------|-------|
| Topology | Triangle list |
| Rasterization | Fill, back-face culling, CCW front face |
| Depth | Test enabled (LESS_OR_EQUAL), write enabled |
| Blending | Disabled (opaque) |
| Viewport/Scissor | Dynamic |

**Descriptor set layout**: binding 0 = uniform buffer (FrameUniforms, vertex stage only)

**Push constant range**: 80 bytes (mat4 world + vec4 color), vertex stage only

### Shaders

**Vertex shader** (`shaders/megacity_scene.vert`):

```glsl
layout(set=0, binding=0) uniform FrameUniforms {
    mat4 view, proj;
    vec4 light_dir;
} frame;

layout(push_constant) uniform ObjectUniforms {
    mat4 world;
    vec4 color;
} object_data;

layout(location=0) in vec3 in_position;
layout(location=1) in vec3 in_normal;
layout(location=2) in vec3 in_color;
layout(location=0) out vec3 out_color;

void main() {
    vec4 world_pos = object_data.world * vec4(in_position, 1.0);
    vec3 normal_ws = normalize(mat3(object_data.world) * in_normal);
    vec3 light = normalize(-frame.light_dir.xyz);
    float ndotl = max(dot(normal_ws, light), 0.0);
    float lighting = 0.2 + 0.8 * ndotl;

    out_color = in_color * object_data.color.rgb * lighting;
    gl_Position = frame.proj * frame.view * world_pos;
}
```

Per-vertex Lambertian: 20% ambient + 80% diffuse. Color = vertex_color x object_color x lighting. Light direction: (-0.5, -1.0, -0.3) normalized — from upper-right-back.

**Fragment shader** (`shaders/megacity_scene.frag`):

```glsl
layout(location=0) in vec3 in_color;
layout(location=0) out vec4 out_frag_color;

void main() {
    out_frag_color = vec4(in_color, 1.0);
}
```

Passthrough: interpolated vertex color, alpha=1.

### The record() Call

Step by step in `IsometricScenePass::record(IRenderContext& ctx)`:

```
1. Cast to VkRenderContext, get command buffer and frame index
2. state_->ensure(*vk_ctx)           — create/validate pipeline if needed
3. state_->reclaim_retired_meshes()  — free old grid meshes whose GPU work is done
4. state_->ensure_floor_grid()       — regenerate grid mesh if FloorGridSpec changed
5. Upload FrameUniforms: memcpy(view, proj, light_dir) → flush

6. vkCmdBindPipeline(cmd, GRAPHICS, pipeline)
7. vkCmdBindDescriptorSets(cmd, GRAPHICS, layout, 0, 1, &descriptor_set)

8. For each SceneObject (cubes):
   a. Select mesh (cube_mesh)
   b. ObjectPushConstants = { obj.world, obj.color }
   c. vkCmdBindVertexBuffers(cmd, 0, 1, &cube_verts, &offset)
   d. vkCmdBindIndexBuffer(cmd, cube_indices, 0, UINT16)
   e. vkCmdPushConstants(cmd, layout, VERTEX_BIT, 0, 80, &push)
   f. vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0)
      → 36 indices, 1 instance, renders 12 triangles

9. Floor grid (if enabled):
   a. ObjectPushConstants = { identity, grid_color }
   b. vkCmdBindVertexBuffers(cmd, 0, 1, &grid_verts, &offset)
   c. vkCmdBindIndexBuffer(cmd, grid_indices, 0, UINT16)
   d. vkCmdPushConstants(cmd, layout, VERTEX_BIT, 0, 80, &push)
   e. vkCmdDrawIndexed(cmd, grid_index_count, 1, 0, 0, 0)
      → ~84 indices for a 5×5 grid, renders ~28 triangles
```

**Per-frame GPU cost**: 1 pipeline bind, 1 descriptor bind, then per-object: vertex/index bind + push constants + indexed draw. Currently 2 draw calls total (1 cube + 1 grid).

### Memory Budget

| Resource | Size | Lifetime |
|----------|------|----------|
| Cube mesh (24 verts × 36 bytes) | 864 B vertices + 72 B indices | Static (created once) |
| Grid mesh (~56 verts × 36 bytes) | ~2 KB vertices + 168 B indices | Dynamic (recreated on camera move) |
| FrameUniforms × 2 frames | 288 B (144 × 2) | Persistent, updated each frame |
| Push constants per draw | 80 B (register space, not allocated) | Per-draw |

### Contrast: Grid Renderer vs MegaCity Renderer

| Aspect | Terminal Grid | MegaCity 3D |
|--------|--------------|-------------|
| Geometry | Procedural (6 verts in shader, no VBO) | Traditional vertex/index buffers |
| Data source | SSBO indexed by gl_InstanceIndex | Vertex fetch from bound VBO |
| Draw strategy | Instanced: 1 draw = all cells | Per-object: 1 draw per mesh |
| Per-draw data | Push constants (28B: screen metrics) | Push constants (80B: world matrix + color) |
| Per-frame data | None (cell data is in SSBO) | Uniform buffer (144B: view/proj/light) |
| Blending | BG opaque, FG alpha-blended | Opaque only |
| Depth | Disabled (2D, z=0) | Enabled (LESS_OR_EQUAL) |
| Culling | Scissor rect per pane | Back-face culling + frustum grid culling |
| Scales to | Thousands of cells in 2 draws | Needs instancing/batching for many objects |
