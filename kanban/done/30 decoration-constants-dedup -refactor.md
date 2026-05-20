# Decoration Constants Deduplication

**Type:** refactor
**Priority:** 30
**Raised by:** Claude, Gemini

## Summary

Underline/undercurl position and decoration metric constants are defined independently in `shaders/grid.metal` (Metal) and `shaders/grid_bg.frag` (GLSL). The two copies can drift out of sync silently. Establish a single source of truth for each platform and include it in both shaders.

## Background

Shader constants that control visual decoration positions (underline Y offset, underline thickness, undercurl amplitude) need to match between the background and foreground shader passes to produce coherent rendering. Currently, both the Metal and GLSL shaders define these constants independently. A maintainer who adjusts the underline position in one shader must remember to update the other; there is no enforcement mechanism. Claude and Gemini both identified this as a maintenance risk.

## Implementation Plan

### Files to modify
- `shaders/` — create a shared include for each platform:
  - `shaders/decoration_constants.h` — for Metal shaders
  - `shaders/decoration_constants.glsl` — for GLSL shaders
- `shaders/grid.metal` — include the shared Metal constants header instead of redeclaring
- `shaders/grid_bg.frag` and any other GLSL shader that uses decoration constants — include the shared GLSL constants file

### Steps
- [x] Audit `shaders/grid.metal` and `shaders/grid_bg.frag` (and any companion shaders) for all decoration-related constants
- [x] List the constants: underline Y offset, underline thickness, undercurl period, undercurl amplitude, strikethrough Y offset, etc.
- [x] Create `shaders/decoration_constants.glsl` with the GLSL versions
- [x] Create `shaders/decoration_constants.h` with the Metal versions
- [x] Replace all inline constant definitions in the shaders with `#include` directives pointing to the shared files
- [x] Update `cmake/CompileShaders_Metal.cmake` to pass `-I ${SHADER_SOURCE_DIR}` and depend on the header
- [x] Update `cmake/CompileShaders.cmake` to pass `-I ${SHADER_SOURCE_DIR}` and depend on `.glsl` includes
- [x] Verify the Metal shader compiles via `xcrun` and the GLSL shaders compile via `glslc`
- [x] Run smoke tests to confirm rendering output is unchanged

## Depends On
- None

## Blocks
- None

## Notes
Metal `#include` works with the `-I` flag to `xcrun metal`. GLSL `#include` uses `GL_GOOGLE_include_directive` extension and `-I` flag to `glslc`. Both CMake compilation steps were updated to pass the shaders directory as an include path and to declare a dependency on the shared include file so changes trigger recompilation.

> Work item produced by: claude-sonnet-4-6
> Completed by: claude-sonnet-4-6 (2026-03-19)
