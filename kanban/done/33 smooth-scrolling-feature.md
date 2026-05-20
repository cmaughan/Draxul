# 33 Smooth Scrolling

## Why This Exists

Neovim's grid model produces discrete `grid_scroll` events — rows move by integer cell counts.
On high-refresh-rate displays or trackpad scrolling, this produces a visually jarring "jump" rather
than the fluid animation users expect from modern applications.

SDL3 delivers high-resolution precision scroll events from trackpads (momentum-based, sub-line
deltas) that Draxul currently treated as discrete wheel clicks.

**Source:** `app/app.cpp` — `on_mouse_wheel()` handler, SDL `SDL_EVENT_MOUSE_WHEEL`.
**Raised by:** GPT, Gemini (both list smooth scrolling as a top QoL feature).

## Goal

Implement interpolated scroll animation:
1. Accumulate fractional scroll deltas from SDL precision scroll events.
2. Commit a `nvim_input` scroll command only when the accumulated delta crosses one cell height.
3. Render an interpolated vertical offset during the sub-cell accumulation phase using a pixel-offset
   uniform in the vertex shader (both Vulkan and Metal).
4. A config option `smooth_scroll = true/false`.

## Implementation Plan

- [x] Read `app/app.cpp` `on_mouse_wheel()` and the SDL scroll event fields.
- [x] Read the Vulkan and Metal vertex shaders to understand how cell positions are computed.
- [x] Design the scroll state machine in `InputDispatcher`:
  - `pending_scroll_y_`: accumulated fractional cells.
  - When `|pending_scroll_y_| >= 1.0`: send scroll input to host, decrement by 1.0.
  - While fraction is non-zero: `scroll_fraction()` exposed for App to query.
- [x] Add `scroll_offset_px` uniform to both GLSL (`grid_bg.vert`, `grid_fg.vert`) and Metal (`grid.metal`) shaders.
- [x] Update `IGridRenderer` interface with `set_scroll_offset(float px)`.
- [x] Implement in `MetalRenderer` and `VkRenderer`, pass in push_data each frame.
- [x] Update `vk_pipeline.cpp` push constant range size (4 → 5 floats).
- [x] Add `smooth_scroll` bool to `AppConfig` (default true), parse/serialize from TOML.
- [x] Wire `smooth_scroll` from config to `InputDispatcher::Deps`.
- [x] App frame loop: call `set_scroll_offset(fraction * cell_h)` before `begin_frame()`.
- [x] Add `set_scroll_offset` to all test fake renderers.
- [x] Build clean; all tests pass.

## Notes

SDL3's `wheel.x` / `wheel.y` fields are already `float` (sub-line precision from trackpads).
No SDL API change needed — the existing wheel event already delivers fractional values.

The `scroll_offset_px` uniform shifts the entire terminal viewport by a sub-pixel amount.
Positive = shift content upward (anticipating upward scroll). The shader subtracts the offset
from `pos.y` so positive fraction moves content up on screen.

Sign convention: `pending_scroll_y_ > 0` means scrolling up (SDL dy > 0), offset moves content up.

*Work item implemented by claude-sonnet-4-6*
