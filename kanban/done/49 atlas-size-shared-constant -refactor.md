# 49 Atlas Size Shared Constant

## Why This Exists

The glyph atlas size (`2048`) is hardcoded as `static constexpr int ATLAS_SIZE = 2048` independently in both the Metal renderer and the Vulkan renderer. If one backend is updated to 4096 and the other is not, textures will be mismatched and test comparisons will produce silently wrong results.

Identified by: **Claude** (smells #14).

## Goal

Define `DRAXUL_ATLAS_SIZE` once in `libs/draxul-types/include/draxul/types.h` (or a new `renderer_constants.h` in `draxul-types`) and reference it from both backends.

## Implementation Plan

- [x] Read `libs/draxul-renderer/src/metal/metal_renderer.mm` and `libs/draxul-renderer/src/vulkan/vk_renderer.cpp` (or equivalent) to locate the `ATLAS_SIZE` constant in each.
- [x] Read `libs/draxul-types/include/draxul/types.h` to determine the best location for a shared constant.
- [x] Add `inline constexpr int kAtlasSize = 2048;` (or `DRAXUL_ATLAS_SIZE`) to `draxul-types`.
- [x] Replace both backend local constants with the shared constant.
- [x] Build both platform targets and verify no compile errors.
- [x] Run `ctest` and `clang-format`.

## Sub-Agent Split

Single agent. Purely mechanical.
