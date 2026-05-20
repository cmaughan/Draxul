# Refactor: Replace void* in IRenderContext with Typed Variant

**Type:** refactor
**Priority:** 19
**Source:** Claude (bad-thing #1), GPT (bad-thing #4) — two-agent strong agreement

## Problem

`libs/draxul-renderer/include/draxul/base_renderer.h` exposes:

```cpp
void* native_command_buffer();    // Metal: id<MTLCommandBuffer>
void* native_render_encoder();    // Metal: id<MTLRenderCommandEncoder> / Vulkan: VkCommandBuffer
```

Every render-pass author must:
1. Know which platform they are on.
2. Cast the `void*` to the correct platform type.
3. Get the cast right or produce a silent crash.

This is the most type-unsafe point in the otherwise excellent renderer hierarchy. A new backend, a test stub returning the wrong type, or a cross-compilation mistake produces a silent crash at the cast site — not a compile error.

**Coordinate with icebox `25 renderer-backend-parity-cleanup -refactor`**: that item addresses the RendererState dirty-region mismatch; this item addresses the context type-safety. They touch the same abstraction layer but have different goals. Sequence this refactor after `25` lands, or ensure the two are done by different agents who communicate the interface changes.

## Proposed design

Option A (preferred — minimal ABI surface):

```cpp
#ifdef __APPLE__
struct MetalContext {
    id<MTLCommandBuffer> command_buffer;
    id<MTLRenderCommandEncoder> encoder;
};
using NativeContext = MetalContext;
#else
struct VulkanContext {
    VkCommandBuffer command_buffer;
    // etc.
};
using NativeContext = VulkanContext;
#endif

// In IRenderContext:
const NativeContext& native_context() const;
```

Option B (more flexible — supports future backends):

```cpp
using NativeContext = std::variant<MetalContext, VulkanContext>;
const NativeContext& native_context() const;
```

Option A is simpler for a two-platform codebase. Option B is better if a third backend (e.g. D3D12) is anticipated.

## Implementation steps

- [x] Read `base_renderer.h` — find all `void*` accessor methods and their call sites.
- [x] Find every render pass that calls `native_command_buffer()` / `native_render_encoder()` (MegaCity cube pass, any others).
- [x] Define `MetalContext` / `VulkanContext` structs in a platform-specific header or via conditional compilation in `base_renderer.h`.
- [x] Change `IRenderContext` to return a reference to the typed struct.
- [x] Update `MetalRenderer` and `VkRenderer` implementations.
- [x] Update all render-pass call sites to use the typed accessor.
- [x] Build on both platforms (or at minimum macOS; CI covers Windows).
- [x] Run tests to confirm no regressions.

## Acceptance criteria

- [x] No `void*` in `IRenderContext` public API.
- [x] Misuse of the accessor (e.g. using Metal context on Vulkan) produces a compile error or a `std::bad_variant_access`, not a silent crash.
- [x] All existing render passes compile and produce correct output.

## Interdependencies

- **Icebox `25 renderer-backend-parity-cleanup -refactor`**: touches same abstraction layer; coordinate scopes to avoid conflicting interface changes.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
