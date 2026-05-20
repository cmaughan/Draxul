# 12 CLAUDE.md Documentation Errors

## Why This Exists

`CLAUDE.md` is the primary reference document for AI agents working in this codebase. It currently contains two factual errors that will mislead any agent doing renderer work:

1. **"96 bytes/cell"** — The actual `static_assert` in the codebase reads `sizeof(GpuCell) == 112`. The documented value is wrong by 16 bytes.
2. **"R8 texture"** for the glyph atlas — The actual format is RGBA8 (4 bytes/pixel). `glyph_cache.cpp` and the shaders both confirm RGBA8.

These are not cosmetic; an agent working on the renderer, atlas upload, or buffer sizing math will use these constants directly.

**Source:** `CLAUDE.md` Rendering section. Cross-referenced against `libs/draxul-renderer/src/renderer_state.h` (`static_assert`) and `libs/draxul-font/src/glyph_cache.cpp`, `libs/draxul-renderer/src/vulkan/vk_atlas.cpp`, and `libs/draxul-renderer/src/metal/metal_renderer.mm`.
**Raised by:** Claude.

## Goal

Correct both errors in `CLAUDE.md` so the document accurately describes the current implementation.

## Implementation Plan

- [x] Confirmed `sizeof(GpuCell)` by reading the `static_assert` in `libs/draxul-renderer/src/renderer_state.h`.
- [x] Confirmed atlas pixel format by reading `libs/draxul-font/src/glyph_cache.cpp`, `libs/draxul-renderer/src/vulkan/vk_atlas.cpp`, and `libs/draxul-renderer/src/metal/metal_renderer.mm`.
- [x] Updated the "96 bytes/cell" reference in `CLAUDE.md` to the correct value.
- [x] Updated the "R8 texture" reference in `CLAUDE.md` to the correct format.
- [x] Scanned the rest of the Rendering section for other stale numeric constants while making the targeted fixes.

## Tests

- [x] Documentation-only change verified by reading the updated `CLAUDE.md` section and cross-checking it with the current renderer and font sources.

## Sub-Agent Split

Single agent. This is a documentation-only fix.
