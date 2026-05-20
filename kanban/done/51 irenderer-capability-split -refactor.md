# 51 IRenderer Capability Split

## Why This Exists

`IRenderer` currently owns: grid rendering, atlas upload, cursor rendering, frame capture for CI, ImGui backend lifecycle, and background color policy. This forces both Metal and Vulkan backends to implement debug/UI concerns and test-capture concerns as part of their core contract. It increases API churn and makes it harder to add a new backend or test a backend in isolation.

Identified by: **GPT** (finding #3), **Claude** (test code compiled into production, bad things #2), **Gemini** (debug overlay entangled with render loop).

## Goal

Split `IRenderer` into focused interfaces:
- `IGridRenderer` — grid cells, atlas upload, cursor, BG/FG passes, swap/present
- `IDebugRenderer` / `IImGuiHost` — ImGui frame lifecycle (begin/end/draw data)
- `ICaptureRenderer` — frame capture (only needed in render-test builds)

Backends implement all three (since they're the same object), but `App` only sees `IGridRenderer` by default. The capture and debug interfaces are cast to only when needed.

## Implementation Plan

- [x] Read `libs/draxul-renderer/include/draxul/renderer.h` to list all current `IRenderer` methods.
- [x] Group methods by concern: grid (write_cells, begin/end_frame, present), debug (begin_imgui_frame, set_imgui_draw_data), capture (capture_frame, finish_capture_readback).
- [x] Create `IGridRenderer`, `IImGuiHost`, and `ICaptureRenderer` pure interfaces in `renderer.h`.
- [x] Have concrete `MetalRenderer` and `VkRenderer` inherit all three (via `IRenderer` combined type, unchanged).
- [x] Update `App` to hold `IGridRenderer*`, `IImGuiHost*`, and `ICaptureRenderer*` typed pointers alongside the owning `unique_ptr<IRenderer>`.
- [x] Update debug panel code to use `imgui_host_` typed pointer.
- [x] Update render test code to use `capture_renderer_` typed pointer.
- [x] Update `GridRenderingPipeline` to hold `IGridRenderer*` instead of `IRenderer*`.
- [x] Build macOS target: clean compile, no errors.
- [x] Run `ctest`: same result as baseline (pre-existing test crash unrelated to this change).
- [x] `clang-format` all touched files.

## Sub-Agent Split

Two agents possible: one refactors the interface header and Metal backend, another does Vulkan backend and App wiring. Both depend on the interface design being finalised first.

## Notes

- `IRenderer` is kept as a combined type that inherits `IGridRenderer`, `IImGuiHost`, and `ICaptureRenderer` for full backward compatibility. Test `FakeRenderer` classes inherit `IRenderer` unchanged.
- `App` initializes `grid_renderer_`, `imgui_host_`, and `capture_renderer_` raw pointers from `renderer_.get()` after renderer creation; ownership stays in `unique_ptr<IRenderer>`.
- The pre-existing `draxul-tests` crash (exits after app config tests) is not caused by this change — confirmed by reproducing on baseline before and after applying the diff.
