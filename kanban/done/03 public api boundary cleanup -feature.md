
# 03 Public API Boundary Cleanup

## Why This Exists

The repo-level module split is good, but several public surfaces still leak implementation detail or collapse unrelated concerns into one header.

Current verified issues:

- `IWindow` exposes `SDL_Window*`
- `nvim.h` still bundles process, RPC, value model, redraw, and input concerns
- `font.h` is just a passthrough
- `GLYPH_ATLAS_SIZE` still lives in shared `draxul-types`

## Goal

Reduce rebuild/merge blast radius and make module ownership clearer to humans and agents.

## Implementation Plan

1. [x] Clean the window boundary.
   - `IWindow::native_handle()` changed from `SDL_Window*` to `void*`; removed `struct SDL_Window;` forward decl from `window.h`
   - Callers in `vk_renderer.cpp` and `metal_renderer.mm` cast to `SDL_Window*` locally
2. [x] Split `nvim.h`.
   - Created `nvim_rpc.h`: NvimProcess, MpackValue, RPC types, IRpcChannel, NvimRpc
   - Created `nvim_ui.h`: ModeInfo, UiEventHandler, NvimInput
   - `nvim.h` kept as a convenience re-export of both
   - Internal sources and `ui_request_worker.h` updated to narrower headers
3. [x] Remove misleading passthroughs.
   - Deleted `libs/draxul-font/include/draxul/font.h` (nothing included it)
4. [x] Move implementation constants down.
   - Removed `GLYPH_ATLAS_SIZE` from `draxul-types/types.h`
   - Inlined `2048` directly in `font_engine.h`, `metal_renderer.h`, `vk_atlas.h`
   - Removed now-unused `#include <draxul/types.h>` from `vk_atlas.h`

## Tests

- [x] Build passes: `cmake --build build`
- [x] All tests pass: `ctest --test-dir build` (5/5)

## Suggested Slice Order

1. window boundary cleanup
2. font/shared-constant cleanup
3. `nvim` header split

## Sub-Agent Split

- one agent on `draxul-window`
- one agent on font/shared-types cleanup
- one agent on `draxul-nvim` header partitioning
