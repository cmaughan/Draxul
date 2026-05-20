# WI 61 — live-frame-encoding

**Type**: refactor  
**Priority**: 9 (current renderer still defers all GPU encoding until `end_frame()`)  
**Source**: direct architecture follow-up  
**Produced by**: codex

---

## Problem

The per-host immediate draw refactor moved ordering and ownership to the app and hosts, but the backends still batch all GPU work internally:

- hosts call `IFrameContext`
- `IFrameContext` records draw intent into `recorded_commands_`
- backends upload grid state and encode the actual GPU commands only in `end_frame()`

That means:

- the GPU does not start this frame's rendering work until the CPU has finished visiting every host
- grid upload timing is still backend-global rather than host-local
- the frame API is "immediate" only at the orchestration layer, not at the encoder layer

The target is live frame encoding:

- `begin_frame()` opens the real frame command state
- each host encodes its GPU work immediately during `draw(frame)`
- grid state uploads happen at the point of use for that host
- `end_frame()` only closes the frame and submits/presents

---

## Tasks

- [x] Add live-encoding work item notes and scope limits.
- [x] Refactor Vulkan `FrameContext` so `draw_grid_handle()`, `record_render_pass()`, and `render_imgui()` encode immediately.
- [x] Refactor Vulkan grid draws so cursor application and grid buffer upload happen at draw time, not in a backend-wide `end_frame()` sweep.
- [x] Open Vulkan frame command recording in `begin_frame()` and close/submit it in `end_frame()`.
- [x] Remove Vulkan `recorded_commands_` replay state.
- [x] Refactor Metal `FrameContext` so `draw_grid_handle()`, `record_render_pass()`, and `render_imgui()` encode immediately.
- [x] Refactor Metal grid draws so cursor application and buffer upload happen at draw time.
- [x] Open the Metal command buffer in `begin_frame()` and commit/present it in `end_frame()`.
- [x] Remove Metal `recorded_commands_` replay state.
- [x] Validate MegaCity + zsh split, diagnostics host, and command palette overlay still render in app order.
- [x] Build and run smoke validation.

---

## Scope Notes

This item intentionally lands the first live-encoding step with **one final frame submit/present**. That gives us:

- host-local encoding
- host-local grid uploads
- no deferred backend replay list

but it does **not yet** split the frame into multiple queue submissions per host.

That follow-up remains desirable for more CPU/GPU overlap, but it is a second step because it adds more queue and attachment lifetime complexity.

One current constraint for Vulkan in this first step:

- `record_render_pass()` must occur before the first grid or ImGui draw that opens the main window render pass for that frame

That matches the current MegaCity usage and keeps the first live-encoding change safe without redesigning the Vulkan main render pass into a resumable load-based sequence.

---

## Acceptance Criteria

- `IFrameContext` no longer appends to `recorded_commands_` in either backend.
- Grid handles upload their own dirty state at draw time.
- `end_frame()` no longer performs a renderer-wide grid upload sweep.
- MegaCity, zsh split, diagnostics host, and command palette overlay still render in the app's host order.
- `cmake --build build --target draxul draxul-tests` passes.
- `python3 do.py smoke` passes.

---

## Status

The first live-encoding step is implemented.

What changed:

- both backends now open the real frame command state in `begin_frame()`
- `IFrameContext` methods encode work immediately
- grid cursor application and GPU uploads now happen at the host draw point
- `end_frame()` now closes the frame and submits / presents it

What is still intentionally deferred to a later item:

- splitting a frame across multiple queue submissions or command buffers to overlap GPU execution with later host preparation
- making Vulkan's main window render pass resumable after an earlier grid or ImGui draw

---

## Notes for Agent

- Keep shared immutable renderer assets shared: pipelines, atlas texture, sampler, swapchain or drawable, sync objects.
- The isolation boundary is per-host mutable draw state, not full per-host duplication of renderer-global device objects.
- If Vulkan needs a later follow-up for resumable main render passes or per-host queue submits, keep that as a separate work item rather than mixing it into this one.
