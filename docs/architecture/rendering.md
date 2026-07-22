# Rendering Flow

This is the current render architecture after the per-host immediate draw refactor.

## Ownership

- `App` orchestrates frame order.
- `PaneManager` owns pane hosts in split-tree order.
- Each grid-capable host owns its own `IGridHandle`.
- Each `IGridHandle` owns its own per-frame GPU grid buffer.
- `MegaCityHost` owns its scene pass and records its own 3D work.
- `DiagnosticsPanelHost` owns the diagnostics ImGui panel.
- `CommandPaletteHost` owns the palette grid handle and draws last as an overlay.
- The renderer owns only global/shared GPU resources plus frame lifecycle:
  - swapchain / drawable
  - frame command buffer / encoder
  - glyph atlas texture + sampler
  - grid pipelines
  - backend frame synchronization objects

## High-Level Loop

For a larger standalone diagram, open [rendering-flowchart.md](/Users/cmaughan/dev/Draxul/docs/architecture/rendering-flowchart.md) directly in Obsidian.

```mermaid
flowchart TD
    A[App renders frame] --> B[App updates diagnostics state]
    B --> C[Renderer begins frame]
    C --> D[App walks hosts in pane order]
    D --> E[Each host encodes its own draw work immediately]
    E --> F[Grid uploads and grid draws happen at the host draw point]
    F --> G[App flushes completed host chunks when it moves to the next host]
    G --> H[Diagnostics host encodes bottom split ImGui]
    H --> I[Command palette host encodes overlay draw]
    I --> J[Renderer ends frame]
    J --> K[Backend submits the final open chunk]
    K --> L[Backend presents the final drawable]
```

## Example: MegaCity + Zsh Split

If the split tree is:

- left: `MegaCityHost`
- right: `Zsh` terminal host

and diagnostics is visible, with the command palette open, the effective draw order is:

1. `MegaCityHost::draw(frame)`
2. `ZshHost::draw(frame)`
3. `DiagnosticsPanelHost::draw(frame)`
4. `CommandPaletteHost::draw(frame)`
5. `renderer->end_frame()`

That means:

- MegaCity records its 3D pass and its host-local ImGui.
- Zsh records its pane-local grid draw.
- The app flushes MegaCity and zsh as completed chunks before moving on.
- Diagnostics records panel ImGui into the bottom split region.
- Command palette records its full-window overlay grid on top of everything.

## Detailed Flow

```mermaid
flowchart TD
    App[App renders frame] --> Update[App updates diagnostics state]
    Update --> Begin[Renderer begins frame]
    Begin --> Frame[Frame context created]

    Frame --> Hosts[App walks hosts in pane order]
    Hosts --> Mega[MegaCity host draws]
    Hosts --> Zsh[Zsh host draws]

    Mega --> Mega3D[Encode MegaCity scene pass for its pane]
    Mega --> MegaUI[Encode MegaCity ImGui draw data]
    MegaUI --> MegaFlush[App flushes MegaCity chunk]
    Zsh --> ZshGrid[Apply zsh cursor and upload zsh grid buffer]
    ZshGrid --> ZshDraw[Encode zsh grid draw]
    ZshDraw --> ZshFlush[App flushes zsh chunk]

    Hosts --> Diag[Diagnostics host draws]
    Diag --> DiagUI[Encode diagnostics ImGui draw data]
    DiagUI --> DiagFlush[App flushes diagnostics chunk]

    Diag --> PalPump[Command palette updates if open]
    PalPump --> PalCells[Build palette cells and update palette grid handle]
    PalCells --> PalUpload[Apply palette cursor and upload palette grid buffer]
    PalUpload --> PalDraw[Encode palette grid draw]

    Mega3D --> End[Renderer ends frame]
    MegaFlush --> End
    ZshFlush --> End
    DiagFlush --> End
    PalDraw --> End

    MegaFlush --> ChunkA[Backend submits MegaCity chunk early]
    ZshFlush --> ChunkB[Backend submits zsh chunk early]
    DiagFlush --> ChunkC[Backend submits diagnostics chunk early]
    End --> Final[Backend submits the final open chunk]
    ChunkA --> GPU3D[GPU executes MegaCity 3D pass]
    ChunkB --> GPUZsh[GPU draws zsh grid from zsh handle buffer]
    ChunkC --> GPUDiag[GPU draws diagnostics ImGui in bottom split region]
    Final --> GPUPal[GPU draws command palette overlay grid last]
    GPU3D --> Present[Present drawable or swapchain image]
    GPUZsh --> Present
    GPUDiag --> Present
    GPUPal --> Present
```

## GPU Objects By Content Type

### Shared renderer-owned objects

- swapchain images / Metal drawable
- frame command buffer or Metal command buffer
- glyph atlas texture
- glyph atlas sampler
- grid background pipeline
- grid foreground pipeline
- Vulkan descriptor pool for per-handle descriptor sets
- per-frame synchronization objects

### Terminal / grid host objects

Each grid host owns:

- CPU-side `RendererState`
- pane viewport / scissor rectangle
- per-frame GPU grid buffer
  - Metal: one `MTLBuffer` per frame slot
  - Vulkan: one `VkGridBuffer` per frame slot
- Vulkan only: per-frame background / foreground descriptor sets

At draw time the host calls `draw_grid_handle(handle)`, and the backend immediately:

- the handle's own GPU buffer
- the shared atlas texture/sampler
- the shared grid pipelines

and draws that handle with `baseInstance / firstInstance = 0`.

### MegaCity objects

`MegaCityHost` owns:

- scene/world/camera state
- `IRenderPass` implementation for the scene
- any MegaCity-specific GPU resources managed by that pass
- MegaCity ImGui context + draw data

At draw time it records:

- `record_render_pass(...)` for 3D
- `render_imgui(...)` for MegaCity UI

### Diagnostics panel objects

`DiagnosticsPanelHost` owns:

- `UiPanel`
- diagnostics ImGui context
- dockspace/window state

It records only:

- `render_imgui(...)`

### Command palette objects

`CommandPaletteHost` owns:

- palette state / filtering state
- one lazily-created full-window `IGridHandle`

When open, it:

1. computes palette cells
2. updates its own grid handle
3. records `draw_grid_handle(...)`

## Backend Chunked Submission Model

Both Metal and Vulkan now follow the same pattern:

1. `begin_frame()` acquires the frame target and returns an `IFrameContext`.
2. Hosts encode their draws immediately into the live frame context.
3. Grid hosts apply cursor state and upload their own dirty grid data at draw time.
4. The app flushes completed host chunks at host boundaries so the backend can submit earlier work while the CPU continues encoding later hosts.
5. `end_frame()` submits the final open chunk and presents the composed frame.

That is the key architectural boundary:

- `App` decides ordering.
- Hosts decide what to draw.
- The renderer manages live frame/device concerns plus chunk submission.
