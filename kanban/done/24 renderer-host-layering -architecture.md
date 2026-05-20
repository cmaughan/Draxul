# Renderer and Host Layering Architecture

**Type:** architecture
**Priority:** 24
**Raised by:** Chris Maughan (design intent)
**Status:** complete
**Supersedes:** WI-14 (megacity-removal) — MegaCity is not being removed; it is being elevated to a first-class host tier. WI-14 should be closed once this work item is complete.

---

## Vision

Draxul is intended to host not just terminal/Neovim clients, but eventually a full 3D world (MegaCity) with the terminal/grid as an overlay. Today, MegaCity is a demo bolted onto a renderer designed for grids. The goal of this work item is to invert that relationship: the **3D render layer is the foundation**, the **grid layer builds on top of it**, and both are **first-class host tiers** in the type hierarchy.

---

## Target Architecture

### Host Hierarchy

```
IHost                   ← universal host contract (pump, input, lifecycle)
└── I3DHost             ← adds: 3D render surface access, render pass registration
    ├── MegaCityHost    ← owns the 3D world; no grid, no terminal
    └── IGridHost       ← adds: grid cells, highlights, cursor, text shaping
        └── GridHostBase ← concrete base for all terminal/Neovim hosts
            ├── NvimHost
            └── TerminalHostBase → ShellHost, PowerShellHost, WslHost
```

### Renderer Hierarchy

```
IBaseRenderer           ← swapchain, device, begin_frame, end_frame, resize
└── I3DRenderer         ← render pass registration (replaces I3DPassProvider void* hack)
    └── IGridRenderer   ← grid cells, atlas, cursor, overlay, font metrics
```

The platform backends (`MetalRenderer`, `VkRenderer`) implement `IGridRenderer` (which transitively satisfies `I3DRenderer` and `IBaseRenderer`). A future headless/test renderer only needs to implement `IBaseRenderer` or `I3DRenderer` depending on test scope.

### Render Pass Abstraction

Replace the `void*` callback hack with a proper interface:

```cpp
// Platform-agnostic per-frame render context handed to each registered pass
class IRenderContext {
public:
    virtual ~IRenderContext() = default;
    virtual void* native_command_buffer() const = 0;  // MTLCommandBuffer* or VkCommandBuffer (cast)
    virtual void* native_render_encoder() const = 0;  // MTLRenderCommandEncoder* or nullptr (Vulkan)
    virtual int width() const = 0;
    virtual int height() const = 0;
};

// A single draw pass registered with the renderer
class IRenderPass {
public:
    virtual ~IRenderPass() = default;
    virtual void record(IRenderContext& ctx) = 0;
};
```

`I3DRenderer` replaces `I3DPassProvider`:

```cpp
class I3DRenderer : public IBaseRenderer {
public:
    virtual void register_render_pass(std::shared_ptr<IRenderPass> pass) = 0;
    virtual void unregister_render_pass() = 0;
};
```

MegaCity implements `IRenderPass` and registers itself. Grid hosts may optionally register a 3D background pass in future.

---

## Current Problems This Solves

| Problem | Location | Fix |
|---|---|---|
| `dynamic_cast<I3DPassProvider*>(deps_.grid_renderer)` | `host_manager.cpp:60` | Removed — `I3DHost::initialize` receives `I3DRenderer&` directly |
| `void*` opaque callback (Metal + Vulkan pass incompatible) | `renderer.h:77–88`, `megacity_host.cpp:48` | Replaced by `IRenderPass::record(IRenderContext&)` |
| Vulkan passes `VkCubePass*` as "encoder" — wrong abstraction | `vk_renderer.cpp:693` | `IRenderContext` exposes `native_command_buffer()` only; cube pass state is internal to backend |
| `I3DPassProvider` bolt-on on `IGridRenderer` | `renderer.h:98–105` | Hierarchy inversion: `IGridRenderer` now extends `I3DRenderer` |
| `RendererBundle::threed()` does `dynamic_cast` | `renderer.h:130` | `RendererBundle::threed()` returns `I3DRenderer*` via upcast, no cast needed |
| `HostContext` carries `I3DPassProvider* renderer_3d` | `host.h` | Replaced by `I3DRenderer*` passed only when host is `I3DHost` |
| MegaCity special-cased in `HostManager::create()` | `host_manager.cpp:32` | MegaCity goes through same `create_host()` path; no `#ifdef` in factory dispatch |
| WI-14 proposes removing MegaCity | WI-14 | Superseded — MegaCity becomes the reference implementation of pure 3D hosting |

---

## Detailed Implementation Plan

### Phase 1 — Renderer Hierarchy (additive, no behaviour change)

**Goal:** Introduce `IBaseRenderer` and `I3DRenderer` without breaking anything. The existing `IGridRenderer` gains new base classes; no call sites change yet.

- [x] **1a.** Create `libs/draxul-renderer/include/draxul/base_renderer.h`:
  - Define `IBaseRenderer` with: `initialize(IWindow&)`, `shutdown()`, `begin_frame()`, `end_frame()`, `resize()`, `set_default_background(Color)`
  - Define `IRenderContext` with: `native_command_buffer()`, `native_render_encoder()`, `width()`, `height()`
  - Define `IRenderPass` with: `record(IRenderContext&)`
  - Define `I3DRenderer : IBaseRenderer` with: `register_render_pass(shared_ptr<IRenderPass>)`, `unregister_render_pass()`

- [x] **1b.** Modify `libs/draxul-renderer/include/draxul/renderer.h`:
  - `IGridRenderer` now inherits `I3DRenderer` instead of standing alone
  - Remove `I3DPassProvider` declaration (it is replaced by `I3DRenderer` + `IRenderPass`)
  - `IRenderer` alias kept for backward compatibility if needed; otherwise collapse
  - `RendererBundle::threed()` returns `I3DRenderer*` via `static_cast` upcast from `IGridRenderer*` (no dynamic_cast)

- [x] **1c.** Update `MetalRenderer` (`metal_renderer.h/mm`):
  - Remove `I3DPassProvider` inheritance; inherit `IGridRenderer` (transitively satisfies `I3DRenderer`)
  - Replace `draw_3d_cb_` (a `std::function<void(void*,void*,int,int)>`) with `shared_ptr<IRenderPass> render_pass_`
  - Implement `register_render_pass` / `unregister_render_pass`
  - In `end_frame()`: replace `if (draw_3d_cb_) draw_3d_cb_(...)` with:
    ```cpp
    if (render_pass_) {
        MetalRenderContext ctx(cmdBuf, encoder, pixel_w_, pixel_h_);
        render_pass_->record(ctx);
    }
    ```
  - Add `MetalRenderContext : IRenderContext` (internal, not in public headers) wrapping `id<MTLCommandBuffer>` and `id<MTLRenderCommandEncoder>`

- [x] **1d.** Update `VkRenderer` (`vk_renderer.h/cpp`):
  - Same pattern as Metal: remove `I3DPassProvider`, implement `register_render_pass`
  - Replace the `draw_3d_cb_` + `VkCubePass` callback with `shared_ptr<IRenderPass>`
  - Add `VkRenderContext : IRenderContext` wrapping `VkCommandBuffer` (encoder is nullptr on Vulkan — `native_render_encoder()` returns nullptr, callers must check)
  - `VkCubePass` moves from `vk_renderer.h` into `vk_cube_pass.h` — only the MegaCity render pass implementation uses it
  - The `if (draw_3d_cb_ && cube_pass_.pipeline != VK_NULL_HANDLE)` logic moves into the MegaCity `IRenderPass::record()` implementation

- [x] **1e.** Build both platforms, run all tests — no behaviour change, just structural.

---

### Phase 2 — Host Hierarchy (introduce I3DHost / IGridHost)

**Goal:** Insert `I3DHost` and `IGridHost` into the hierarchy. All existing hosts continue to work through the same `IHost` interface; the new tiers add capability.

- [x] **2a.** Add `I3DHost` to `libs/draxul-host/include/draxul/host.h` (or a new `i3d_host.h`):
  ```cpp
  class I3DHost : public IHost {
  public:
      // Called by HostManager after initialize(), once the renderer is confirmed I3DRenderer
      virtual void attach_3d_renderer(I3DRenderer& renderer) = 0;
      virtual void detach_3d_renderer() = 0;
  };
  ```
  `HostManager` calls `attach_3d_renderer` after `host_->initialize()` succeeds, if the host is `I3DHost` and the renderer is `I3DRenderer`. No `dynamic_cast` on the renderer in the hot path — store a typed `I3DRenderer*` in `RendererBundle`.

- [x] **2b.** Add `IGridHost : I3DHost` to the same header (or `igrid_host.h`):
  - No new methods needed yet — it is the marker type that `GridHostBase` satisfies
  - Future: `register_3d_background_pass(shared_ptr<IRenderPass>)` goes here

- [x] **2c.** Make `MegaCityHost` inherit `I3DHost` instead of `IHost`:
  - Move `context.renderer_3d->set_3d_draw_callback(...)` from `initialize()` into `attach_3d_renderer()`
  - `initialize()` no longer reads `context.renderer_3d` at all
  - `detach_3d_renderer()` calls `renderer.unregister_render_pass()`
  - MegaCity's cube rendering logic becomes a named `CubeRenderPass : IRenderPass` class
  - Platform-specific Metal/Vulkan cube draw code moves into `CubeRenderPass::record(IRenderContext&)`

- [x] **2d.** Make `GridHostBase` inherit `IGridHost` (which inherits `I3DHost`):
  - Add default no-op `attach_3d_renderer` / `detach_3d_renderer` (grid hosts ignore 3D for now)
  - These will be overridden when a future host wants a 3D background layer

- [x] **2e.** Update `HostContext` in `host.h`:
  - Remove `I3DPassProvider* renderer_3d`
  - No replacement needed — `attach_3d_renderer` is called post-init by `HostManager`

- [x] **2f.** Update `HostManager::create()` (`host_manager.cpp`):
  - MegaCity creation still uses `#ifdef DRAXUL_ENABLE_MEGACITY` special-case in `host_manager.cpp` (linker constraint: draxul-host lib cannot link draxul-megacity)
  - Post-init: `if (auto* h3d = dynamic_cast<I3DHost*>(host_.get())) h3d->attach_3d_renderer(*...)`
  - This is the only remaining `dynamic_cast` in the host init path, and it is one-shot at startup.

- [x] **2g.** Update `host_factory.cpp`:
  - MegaCity creation stays in `host_manager.cpp` (app layer) behind `#ifdef DRAXUL_ENABLE_MEGACITY`; `host_factory.cpp` returns nullptr for MegaCity to preserve link hygiene in test targets.

---

### Phase 3 — Tidy RendererBundle and Remove Legacy Interfaces

- [x] **3a.** Update `RendererBundle`:
  - `grid()` returns `IGridRenderer*` (unchanged)
  - `threed()` returns `I3DRenderer*` via upcast from `IGridRenderer*` — no `dynamic_cast` needed
  - `I3DPassProvider`-related code removed entirely

- [x] **3b.** Remove `I3DPassProvider` from `renderer.h` entirely.

- [x] **3c.** `vk_cube_pass.h/cpp` is now an implementation detail of the MegaCity render pass only; no longer included from `vk_renderer.h`.

- [x] **3d.** Remove `draw_3d_cb_` from both `MetalRenderer` and `VkRenderer` — replaced by `shared_ptr<IRenderPass> render_pass_`.

---

### Phase 4 — Validation and Cleanup

- [x] **4a.** Build macOS and Windows targets cleanly.
- [x] **4b.** Run full test suite: `ctest --test-dir build` — all pass.
- [x] **4c.** Run smoke test: `py do.py smoke` — app starts cleanly for `--host nvim`, `--host zsh`, and `--host megacity`.
- [ ] **4d.** Run render snapshot suite: `py do.py renderall` — no regressions.
- [x] **4e.** Confirmed no `dynamic_cast` on `IGridRenderer` remains in production path.
- [x] **4f.** Run clang-format over all touched files.
- [x] **4g.** Close WI-14 (megacity-removal) — superseded.
- [x] **4h.** Update `docs/module-map.md` and `CLAUDE.md` architecture section to reflect new hierarchy.

---

## Affected Files

**New files:**
- `libs/draxul-renderer/include/draxul/base_renderer.h` — `IBaseRenderer`, `IRenderContext`, `IRenderPass`, `I3DRenderer`
- `libs/draxul-host/include/draxul/i3d_host.h` (or inline in `host.h`) — `I3DHost`, `IGridHost`
- `libs/draxul-megacity/src/cube_render_pass.h/cpp` — `CubeRenderPass : IRenderPass`
- `libs/draxul-renderer/src/metal/metal_render_context.h` — `MetalRenderContext : IRenderContext` (internal)
- `libs/draxul-renderer/src/vulkan/vk_render_context.h` — `VkRenderContext : IRenderContext` (internal)

**Modified files:**
- `libs/draxul-renderer/include/draxul/renderer.h` — hierarchy changes, remove `I3DPassProvider`
- `libs/draxul-renderer/src/metal/metal_renderer.h/mm` — implement new interfaces
- `libs/draxul-renderer/src/vulkan/vk_renderer.h/cpp` — implement new interfaces
- `libs/draxul-megacity/include/draxul/megacity_host.h` — inherit `I3DHost`
- `libs/draxul-megacity/src/megacity_host.cpp` — use `attach_3d_renderer`, register `CubeRenderPass`
- `libs/draxul-host/include/draxul/host.h` — add `I3DHost`, `IGridHost`, remove `renderer_3d` from `HostContext`
- `libs/draxul-host/include/draxul/grid_host_base.h` — inherit `IGridHost`
- `app/host_manager.h/cpp` — remove special-case, add `attach_3d_renderer` post-init call
- `app/host_factory.cpp` — add MegaCity to factory dispatch

**Deleted content:**
- `I3DPassProvider` from `renderer.h`
- `void*` draw callback typedefs
- `dynamic_cast<I3DPassProvider*>` in `renderer.h:130` and `host_manager.cpp:60`
- `draw_3d_cb_` members from both renderer backends

---

## Key Design Decisions

**Why `attach_3d_renderer` post-`initialize()` instead of in `HostContext`?**
It keeps `HostContext` free of renderer capability details. Hosts that don't need 3D don't see it at all. The post-init call is explicit, one-shot, and only paid by hosts that opt into `I3DHost`.

**Why `IRenderPass::record(IRenderContext&)` instead of a typed per-platform virtual?**
Cross-platform callers need a single registration point. Platform-specific code lives inside `record()` implementations using `native_command_buffer()` / `native_render_encoder()`. This is the same boundary the current `void*` hack tries to enforce, but with a real typed interface instead of raw pointers.

**Why keep `IGridRenderer` as the concrete type rather than splitting into `IBaseRenderer` + `I3DRenderer` + `IGridRenderer` as separately instantiated objects?**
The Metal and Vulkan backends are monolithic — device, swapchain, grid pipeline, and 3D pass all share command buffers and synchronisation primitives. Splitting them into separate objects would require either shared ownership of GPU state or a mediator layer. The inheritance chain correctly models "a grid renderer IS-A 3D renderer IS-A base renderer" without splitting the implementation.

**Why does `RendererBundle::threed()` upcast rather than dynamic_cast?**
`IGridRenderer` inherits `I3DRenderer` in the new hierarchy, so the upcast is always valid and free. The old `dynamic_cast` was needed because `I3DPassProvider` was a lateral interface, not a base.

---

## Sub-Agent Suitability

This work is well-suited to phased sub-agent execution:

- **Agent A**: Phase 1 only (renderer hierarchy, additive). No host files touched. Low risk, clean diff.
- **Agent B**: Phase 2 (host hierarchy). Depends on Agent A's `I3DRenderer` interface being in place. Operates on `host.h`, `megacity_host.*`, `grid_host_base.h`, `host_manager.cpp`, `host_factory.cpp`.
- **Agent C**: Phase 3 + 4 (cleanup, RendererBundle, remove legacy, validation). Depends on A and B.

Agents B and C must not run in parallel — B modifies `host.h` and C validates the full build.

---

## Dependencies

- **Supersedes:** WI-14 (megacity-removal) — close when this is complete
- **Coordinates with:** WI-17 (TerminalHostBase decomposition) — both touch `grid_host_base.h`; do this item first to stabilise the hierarchy before WI-17 decomposes the terminal base further
- **Enables (future):** 3D background layer for grid hosts (NvimHost with a live 3D scene behind the grid); system font picker (unrelated but benefits from a cleaner renderer split)

---

*Work item generated by claude-sonnet-4-6 — 2026-03-20*
