# 34 Background Transparency and Blur

## Why This Exists

Draxul renders with an opaque solid background. Both platform-native compositor effects
(Windows Mica/DWM blur-behind, macOS `NSVisualEffectView`/vibrancy) and simple alpha transparency
are common features in modern terminal emulators and editors.

**Source:** `app/app.cpp` (window creation), `libs/draxul-window/src/sdl_window.cpp`, platform renderer initialization.
**Raised by:** Claude, GPT, Gemini (all three list this as a top QoL feature).

## Goal

Add optional background transparency:
1. A `background_opacity` config value (0.0–1.0, default 1.0).
2. When `background_opacity < 1.0`, the terminal background blends with the content behind the window.
3. On macOS: use `NSVisualEffectView` behind the Metal layer for native blur/vibrancy.
4. On Windows: use DWM `DWMWA_USE_IMMERSIVE_DARK_MODE` + blur-behind for the Mica or acrylic effect.
5. The glyph content layer (FG pass) is always fully opaque — only cell backgrounds become transparent.

## Implementation Plan

- [ ] Read `libs/draxul-window/src/sdl_window.cpp` to understand SDL window creation and how to access the native window handle (for platform compositor API calls).
- [ ] Read the renderer initialization code for both Vulkan and Metal to understand where the swapchain surface is created.
- [ ] **macOS:**
  - Retrieve `NSWindow` from SDL via `SDL_GetProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL)`.
  - Add an `NSVisualEffectView` behind the `CAMetalLayer`.
  - Set the Metal layer's `isOpaque = NO` and the `CAMetalLayer` pixel format to `MTLPixelFormatBGRA8Unorm` with alpha.
  - Update the Metal clear color to include the configured alpha.
- [ ] **Windows:**
  - Use `DwmExtendFrameIntoClientArea` or the undocumented `SetWindowCompositionAttribute` for blur-behind.
  - Set the Vulkan swapchain surface to support pre-multiplied alpha (if the surface supports `VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR`).
- [ ] Add `background_opacity = 1.0` to `AppConfig`.
- [ ] Wire the opacity value to the renderer's clear color alpha channel.
- [ ] Test: set `background_opacity = 0.8`, verify the window background is translucent.
- [ ] Run `ctest --test-dir build`.

## Notes

The text rendering FG pass must remain fully opaque regardless of background opacity.
Neovim sends `Normal` background colors — these need their alpha premultiplied by `background_opacity`
before being written into the `GpuCell` bg field.

## Sub-Agent Split

- One agent on macOS `NSVisualEffectView` integration.
- One agent on Windows DWM blur-behind + Vulkan composite alpha.
- One agent on `AppConfig` and per-cell alpha premultiplication in `RendererState`.
