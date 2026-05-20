# 58 Configurable Atlas Size

## Why This Exists

The glyph atlas is fixed at 2048×2048. Users with emoji-heavy or large-font configurations exhaust the atlas quickly, triggering silent full-cache resets that cause one-frame visual glitches. The debug panel reports the reset count, but there is no user action available to increase the atlas size.

Identified by: **Claude** (QoL #9). Also addressed by item 49 (sharing the constant).

## Goal

Add an `atlas_size` key to `config.toml` (default 2048, valid range 1024–8192, must be a power of two). At startup, initialise both Metal and Vulkan renderer atlases to the configured size.

## Implementation Plan

- [x] Complete item 49 first (shared atlas constant) — this item extends it with runtime config.
- [x] Read `app/app_config.h` for `AppConfig` structure.
- [x] Add `int atlas_size = kAtlasSize;` to `AppConfig` with validation (clamp to power-of-two in range).
- [x] Add `atlas_size` parsing in `app_config.cpp`.
- [x] Thread `options.atlas_size` through `AppOptions` to renderer creation in `renderer_factory.cpp`.
- [x] Update `MetalRenderer` and `VkRenderer` constructors/init to accept `atlas_size` parameter.
- [x] Replace the hardcoded `kAtlasSize` in both backends with the parameter.
- [x] Add tests verifying `atlas_size = 4096` parses correctly, out-of-range values are clamped, and non-power-of-two values are rounded down.
- [x] Run `ctest`, and `clang-format`.

## Sub-Agent Split

Single agent (sequential on item 49). If item 49 is complete, two agents can work in parallel: one on config/options, one on renderer init parameter.
